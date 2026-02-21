.global jump_to_kernel
jump_to_kernel:
  cli
  /* MS ABI: rcx=entry, rdx=stack, r8=bootinfo */
  mov %rcx, %rax
  mov %rdx, %rsp
  xor %rbp, %rbp
  mov %r8, %rdi
  jmp *%rax
