"""Compile flags for fp L freestanding product code."""
CFLAGS = ['--target=armv7a-none-eabi', '-mcpu=cortex-a9', '-mthumb', '-mfloat-abi=soft',
          '-mfpu=none', '-ffreestanding', '-fno-builtin', '-nostdlib', '-fno-jump-tables',
          '-fropi', '-fno-addrsig', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
          '-Wno-unused-function',
          # every frame 8-aligned, leaf functions too (clang leaves a leaf's frame
          # 4-aligned otherwise; memory arm-stack-alignment, test_stack_alignment.py)
          '-mstackrealign']
