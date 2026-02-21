.global _start
.type _start, @function
_start:
  call kernel_main
.hang:
  hlt
  jmp .hang
