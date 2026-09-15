#include "args/vm_options.h"
#include "serializer/serializer.h"
#include "types/acu_vm_status.h"
#include "vm/vm.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    AcuVmOptions opts;
    char err_buf[256];

    if (!AcuVmOptions_Parse(&opts, argc, argv, err_buf, sizeof(err_buf))) {
        fprintf(stderr, "error: %s\n", err_buf);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (opts.show_help) {
        AcuVmOptions_PrintHelp(argv[0]);
        return EXIT_SUCCESS;
    }

    if (opts.show_version) {
        AcuVmOptions_PrintVersion();
        return EXIT_SUCCESS;
    }

    AcuRawChunk chunk;
    if (!AcuRawChunk_DeserializePath(opts.bytecode_path, &chunk)) {
        fprintf(stderr, "error: '%s' corrupted\n", opts.bytecode_path);
        return EXIT_FAILURE;
    }

    if (opts.verify_only) {
        fprintf(stdout, "'%s' is valid!\n", opts.bytecode_path);
        return EXIT_SUCCESS;
    }

    AcuVM vm;
    AcuVM_Init(&vm, &opts);
    AcuVM_Load(&vm, &chunk);
    AcuVmStatus status = AcuVM_Run(&vm);
    if (unlikely(status != ACU_VM_OK)) {
        fprintf(stderr, "VM finished with status '%s'.\n", AcuVmStatus_String(status));
    }

    AcuVM_Free(&vm);

    return EXIT_SUCCESS;
}
