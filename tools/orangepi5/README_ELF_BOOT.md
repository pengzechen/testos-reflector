# ELF Loader Boot Configuration

This directory contains the U-Boot boot script and helper tools for loading ELF files with the kernel's automatic dependency resolution.

## Files

- **boot.cmd** - U-Boot boot script that loads ELF files and creates the ELF table
- **BOOT_SCRIPT_GUIDE.md** - Comprehensive guide on how to modify boot.cmd
- **generate_elf_entry.sh** - Helper script to generate U-Boot commands for new entries

## Quick Start

### Current Configuration

The `boot.cmd` is pre-configured to load standard musl libraries:
- `libc.so` at 0x80000000
- `libm.so` at 0x80100000  
- `libpthread.so` at 0x80200000
- `libdl.so` at 0x80300000
- `librt.so` at 0x80400000

User programs should be loaded starting from 0x81000000.

### Adding Your Program

1. **Use the helper script:**
   ```bash
   ./generate_elf_entry.sh 5 my_app 0x81000000 0
   ```
   This generates the U-Boot commands to add entry 5.

2. **Add the generated commands to boot.cmd** after the library entries.

3. **Add the file load command** at the top with other ext4load commands:
   ```
   ext4load mmc 1:1  0x81000000 my_app
   setenv myapp_size ${filesize}
   ```

4. **Update the count** in boot.cmd (line ~60):
   ```
   mw.l 0x7F000008 0x00000006   # count: 6 (5 libraries + 1 program)
   ```

5. **Compile boot.scr:**
   ```bash
   mkimage -C none -A arm64 -T script -d boot.cmd boot.scr
   ```

## Memory Map

```
0x00000000 - 0x7EFFFFFF: Kernel and system
0x7F000000:              ELF table (bootloader_elf_table_t)
0x80000000:              libc.so
0x80100000:              libm.so
0x80200000:              libpthread.so
0x80300000:              libdl.so
0x80400000:              librt.so
0x81000000 - 0x8FFFFFFF: User programs (your apps here)
0x90000000+:             Additional libraries/programs if needed
```

## Automatic Dependency Resolution

The kernel's ELF loader automatically:
1. Reads the ELF table at 0x7F000000
2. Parses each ELF's DT_NEEDED entries
3. Finds dependencies in the table
4. Recursively loads dependencies first
5. Loads and relocates the program
6. Applies all relocations

**Example:** If your program depends on libc.so:
- The program's ELF contains DT_NEEDED entry for "libc.so"
- Kernel finds "libc.so" in the table at 0x80000000
- Loads libc.so first (with its dependencies if any)
- Then loads your program
- Resolves all symbols and relocations automatically

## File Placement

Your ELF files should be in the root of the boot partition (MMC 1:1):
```
/boot/
  ├── kernel.uimg
  ├── rk3588-orangepi-5-plus.dtb
  ├── boot.scr (compiled from boot.cmd)
  ├── libc.so
  ├── libm.so
  ├── libpthread.so
  ├── libdl.so
  ├── librt.so
  └── my_app (your executable)
```

## Tips

1. **Use 1MB alignment** for load addresses (e.g., 0x81000000, 0x81100000)
2. **Libraries first** in memory (0x80000000 range), programs after (0x81000000+)
3. **Set flags correctly**: 1 for libraries (.so), 0 for executables
4. **Match names exactly**: Table name must match DT_NEEDED entries
5. **Test incrementally**: Add one program, test, then add more

## Example: Adding init and shell

```bash
# Generate commands
./generate_elf_entry.sh 5 init 0x81000000 0 >> temp_entry.txt
./generate_elf_entry.sh 6 shell 0x81100000 0 >> temp_entry.txt

# Edit boot.cmd:
# 1. Add after library loads:
ext4load mmc 1:1  0x81000000 init
setenv init_size ${filesize}
ext4load mmc 1:1  0x81100000 shell
setenv shell_size ${filesize}

# 2. Update count to 7 (5 libs + 2 programs)
mw.l 0x7F000008 0x00000007

# 3. Add the generated entry commands from temp_entry.txt

# 4. Compile
mkimage -C none -A arm64 -T script -d boot.cmd boot.scr
```

## Troubleshooting

See `BOOT_SCRIPT_GUIDE.md` for detailed troubleshooting and advanced usage.

## For More Information

- ELF Loader API: `../../ELF_LOADER_README.md`
- Quick Reference: `../../ELF_LOADER_QUICK_REF.md`
- Detailed API: `../../docs/elf_loader.md`
