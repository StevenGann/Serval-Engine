// vm_use_saves(): lets scripts save (include/serval/vm.h). On its own in the
// archive, so that only a game that calls it links save.c and a save type's
// code and ROM ID (--gc-sections drops them otherwise, cmake/Serval.cmake).

#include "serval/save.h"
#include "serval/vm.h"
#include "vm_internal.h"

static s32 save_call(u32 fn, u32 slot, void* data, u32 size, u16 version) {
    switch (fn) {
    case VM_SYS_SAVE_WRITE:
        return save_write(slot, data, size, version);
    case VM_SYS_SAVE_READ:
        return save_read(slot, data, size, version);
    case VM_SYS_SAVE_ERASE:
        save_erase(slot);
        return 0;
    case VM_SYS_SAVE_SLOT_VERSION:
        return save_slot_version(slot);
    default: // VM_SYS_SAVE_SLOT_SIZE: at most SAVE_SLOT_MAX
        return (s32)save_slot_size(slot);
    }
}

void vm_use_saves(void) {
    serval_vm_save = save_call;
}
