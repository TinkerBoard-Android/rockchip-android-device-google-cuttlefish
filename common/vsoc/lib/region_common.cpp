#include "common/vsoc/lib/region.h"

#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "common/libs/glog/logging.h"

namespace {
const uint32_t UADDR_OFFSET_MASK = 0xFFFFFFFC;
const uint32_t UADDR_OFFSET_ROUND_TRIP_FLAG = 1;
}  // namespace

using vsoc::layout::Sides;

vsoc::RegionWorker::RegionWorker(RegionView* region)
    : region_(region), thread_(&vsoc::RegionWorker::Work, this) {}

void vsoc::RegionWorker::Work() { region_->ProcessSignalsFromPeer(&stopping_); }

vsoc::RegionWorker::~RegionWorker() {
  stopping_ = true;
  region_->InterruptSelf();
  thread_.join();
}

vsoc::RegionView::~RegionView() {
  if (region_base_ && (region_base_ != MAP_FAILED)) {
    munmap(region_base_, region_size());
  }
}

void vsoc::RegionView::ProcessSignalsFromPeer(volatile bool* stopping) {
  vsoc_signal_table_layout* table = incoming_signal_table();
  const size_t num_offsets = (1 << table->num_nodes_lg2);
  std::atomic<uint32_t>* offsets =
      region_offset_to_pointer<std::atomic<uint32_t>>(
          table->futex_uaddr_table_offset);
  while (!*stopping) {
    WaitForInterrupt();
    if (*stopping) {
      return;
    }
    for (size_t i = 0; i < num_offsets; ++i) {
      uint32_t offset = offsets[i].exchange(0);
      if (offset) {
        bool round_trip = offset & UADDR_OFFSET_ROUND_TRIP_FLAG;
        uint32_t* uaddr =
            region_offset_to_pointer<uint32_t>(offset & UADDR_OFFSET_MASK);
        syscall(SYS_futex, uaddr, FUTEX_WAKE, -1, nullptr, nullptr, 0);
        if (round_trip) {
          SendSignalToPeer(uaddr, false);
        }
      }
    }
  }
}

void vsoc::RegionView::SendSignal(Sides sides_to_signal, uint32_t* uaddr) {
  if (sides_to_signal.value_ & Sides::Peer) {
    // If we should also be signalling our side set the round trip flag on
    // the futex signal.
    bool round_trip = sides_to_signal.value_ & Sides::OurSide;
    SendSignalToPeer(reinterpret_cast<uint32_t*>(uaddr), round_trip);
    // Return without signaling our waiters to give the other side a chance
    // to run.
    return;
  }
  if (sides_to_signal.value_ & Sides::OurSide) {
    syscall(SYS_futex, reinterpret_cast<int32_t*>(uaddr), FUTEX_WAKE, -1,
            nullptr, nullptr, 0);
  }
}

void vsoc::RegionView::SendSignalToPeer(uint32_t* uaddr, bool round_trip) {
  vsoc_signal_table_layout* table = outgoing_signal_table();
  std::atomic<uint32_t>* offsets =
      region_offset_to_pointer<std::atomic<uint32_t>>(
          table->futex_uaddr_table_offset);
  // maximum index in the node that can hold an offset;
  const size_t max_index = (1 << table->num_nodes_lg2) - 1;
  uint32_t offset = pointer_to_region_offset(uaddr);
  if (offset & ~UADDR_OFFSET_MASK) {
    LOG(FATAL) << "uaddr offset is not naturally aligned " << uaddr;
  }
  // Guess at where this offset should go in the table.
  // Do this before we set the round-trip flag.
  size_t hash = (offset >> 2) & max_index;
  if (round_trip) {
    offset |= UADDR_OFFSET_ROUND_TRIP_FLAG;
  }
  while (1) {
    uint32_t expected = 0;
    if (offsets[hash].compare_exchange_strong(expected, offset)) {
      // We stored the offset. Send the interrupt.
      InterruptPeer();
      break;
    }
    // We didn't store, but the value was already in the table with our flag.
    // Return without interrupting.
    if (expected == offset) {
      return;
    }
    // Hash collision. Try again in a different node
    if ((expected & UADDR_OFFSET_MASK) != (offset & UADDR_OFFSET_MASK)) {
      hash = (hash + 1) & max_index;
      continue;
    }
    // Our offset was in the bucket, but the flags didn't match.
    // We're done if the value in the node had the round trip flag set.
    if (expected & UADDR_OFFSET_ROUND_TRIP_FLAG) {
      return;
    }
    // We wanted the round trip flag, but the value in the bucket didn't set it.
    // Do a second swap to try to set it.
    if (offsets[hash].compare_exchange_strong(expected, offset)) {
      // It worked. We're done.
      return;
    }
    if (expected == offset) {
      // expected was the offset without the flag. After the swap it has the
      // the flag. This means that some other thread set the flag, so
      // we're done.
      return;
    }
    // Something about the offset changed. We need to go around again, because:
    //   our peer processed the old entry
    //   another thread may have stolen the node while we were distracted
  }
}

std::unique_ptr<vsoc::RegionWorker> vsoc::RegionView::StartWorker() {
  return std::unique_ptr<vsoc::RegionWorker>(new vsoc::RegionWorker(this));
}

void vsoc::RegionView::WaitForSignal(uint32_t* uaddr, uint32_t expected_value) {
  syscall(SYS_futex, uaddr, FUTEX_WAIT, expected_value, nullptr, nullptr, 0);
}
