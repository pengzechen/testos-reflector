#!/bin/bash
# Helper script to generate U-Boot commands for adding ELF entries to boot.cmd
# Usage: ./generate_elf_entry.sh <entry_index> <name> <load_address> <flags>
#   entry_index: Entry number (0-31)
#   name: ELF filename (e.g., "my_program" or "libc.so")
#   load_address: Memory address in hex (e.g., 0x81000000)
#   flags: 0 for executable, 1 for library

if [ $# -ne 4 ]; then
    echo "Usage: $0 <entry_index> <name> <load_address> <flags>"
    echo "Example: $0 5 my_program 0x81000000 0"
    echo "Example: $0 6 mylib.so 0x81100000 1"
    exit 1
fi

ENTRY_INDEX=$1
NAME=$2
LOAD_ADDR=$3
FLAGS=$4

# Calculate offsets
ENTRY_SIZE=0x58
TABLE_BASE=0x7F000010
ENTRY_START=$((TABLE_BASE + ENTRY_INDEX * ENTRY_SIZE))
NAME_OFFSET=$ENTRY_START
ADDR_OFFSET=$((ENTRY_START + 0x40))
SIZE_OFFSET=$((ENTRY_START + 0x48))
FLAGS_OFFSET=$((ENTRY_START + 0x50))
RESERVED_OFFSET=$((ENTRY_START + 0x54))

echo "# Entry $ENTRY_INDEX: $NAME ($([ $FLAGS -eq 1 ] && echo 'library' || echo 'executable'), flags=$FLAGS)"
echo "# Offset: $(printf '0x%X' $ENTRY_START)"
echo ""
echo "# Write name \"$NAME\" (null-terminated)"

# Convert name to hex bytes
for ((i=0; i<${#NAME}; i++)); do
    char="${NAME:$i:1}"
    hex=$(printf '0x%X' "'$char")
    offset=$((NAME_OFFSET + i))
    echo "mw $(printf '0x%X' $offset) $hex 1         # '$char'"
done

# Add null terminator
null_offset=$((NAME_OFFSET + ${#NAME}))
echo "mw $(printf '0x%X' $null_offset) 0x00 1         # null terminator"
echo ""

# Suggest variable name
VAR_NAME=$(echo "$NAME" | tr '[:upper:]' '[:lower:]' | tr '.' '_' | tr '-' '_')

echo "# Write start_addr (at offset +0x40 = $(printf '0x%X' $ADDR_OFFSET))"
echo "mw.q $(printf '0x%X' $ADDR_OFFSET) $LOAD_ADDR 1  # start_addr: $LOAD_ADDR"
echo ""

echo "# Write size (at offset +0x48 = $(printf '0x%X' $SIZE_OFFSET))"
echo "mw.q $(printf '0x%X' $SIZE_OFFSET) \${${VAR_NAME}_size} 1  # size from filesize"
echo ""

echo "# Write flags (at offset +0x50 = $(printf '0x%X' $FLAGS_OFFSET))"
echo "mw.l $(printf '0x%X' $FLAGS_OFFSET) $(printf '0x%08X' $FLAGS) 1  # flags: $FLAGS ($([ $FLAGS -eq 1 ] && echo 'library' || echo 'executable'))"
echo ""

echo "# Write reserved (at offset +0x54 = $(printf '0x%X' $RESERVED_OFFSET))"
echo "mw.l $(printf '0x%X' $RESERVED_OFFSET) 0x00000000 1  # reserved: 0"
echo ""

echo "# Don't forget to:"
echo "# 1. Load the file: ext4load mmc 1:1 $LOAD_ADDR $NAME"
echo "# 2. Save size: setenv ${VAR_NAME}_size \${filesize}"
echo "# 3. Update count in table header (line ~60): mw.l 0x7F000008 <new_count>"
