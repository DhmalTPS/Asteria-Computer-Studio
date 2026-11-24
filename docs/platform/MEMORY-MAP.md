# Asteria Platform Memory Map

Normative. Decided in D12. Device register layouts will be specified in `DEVICES.md` (milestone M8).

## 1. Physical address space

| Range | Size | Region | Access | Cacheable |
|---|---|---|---|---|
| `0x0000_0000 – 0x0000_FFFF` | 64 KiB | **Boot ROM**. Reset PC = `0x0000_0000` | read, execute | yes |
| `0x0001_0000 – 0x7FFF_FFFF` | – | *unpopulated* | any access → **BusError** | – |
| `0x8000_0000 – 0x8000_0000 + RAM size − 1` | 64 MiB default, ≤ 1 GiB | **RAM** | rwx | yes |
| `… – 0xEFFF_FFFF` | – | *unpopulated* | BusError | – |
| `0xF000_0000 – 0xF00F_FFFF` | 1 MiB | **MMIO**, one 4 KiB page per device | rw (no execute) | **never** |
| `0xF010_0000 – 0xFFFF_FFFF` | – | *unpopulated* | BusError | – |

Rules:
- A write to ROM raises `StoreBusError`. Fetching from MMIO raises `InstrBusError`.
- Cacheability is a property of the **physical** range, never of a PTE. A wrong mapping therefore can't
  cache a device register.
- MMIO registers are 32-bit and must be accessed with `LW`/`SW`. Other sizes raise a BusError.
- Page tables must live in ROM or RAM. `SWAP` is RAM-only (it writes).
- **No devices exist before M8.** Until then every MMIO access is a BusError.

Who may do what, by region (implemented once in `mem/phys/physical_bus`):

| Region | Fetch | Load | Store | SWAP | PTE read | PTE write-back |
|---|---|---|---|---|---|---|
| ROM | yes | yes | – | – | yes | – |
| RAM | yes | yes | yes | yes | yes | yes |
| MMIO | – | word, device page | word, device page | – | – | – |
| unpopulated | – | – | – | – | – | – |

### MMIO device pages
| Base | Device | IRQ |
|---|---|---|
| `0xF000_0000` | Interrupt controller | – |
| `0xF000_1000` | Timer | 0 |
| `0xF000_2000` | Disk controller | 1 |
| `0xF000_3000` | DMA engine | 2 |
| `0xF000_4000` | Keyboard | 3 |
| `0xF000_5000` | Network interface | 4 |
| `0xF000_6000` | Display (text console) | 5 |
| `0xF000_7000` | System control (power-off, exit code, simulator hooks) | – |

One page per device means the kernel can grant or deny each device separately through page tables.

## 2. Virtual address space (paging on, AsteriaOS convention)

```
0xFFFF_FFFF ┌──────────────────────────────┐
            │ MMIO (identity, K only)      │ 0xF000_0000
            ├──────────────────────────────┤
            │ Kernel: identity-mapped RAM  │ U=0, G=1, mapped with 4 MiB superpages
            │ (VA = PA)                    │
0x8000_0000 ├──────────────────────────────┤ ── user/kernel split
            │ guard page (unmapped)        │
0x7FFF_F000 │ user stack ↓                 │
            │ …                            │
            │ heap ↑ (SYS_ALLOC / brk)     │
            │ .bss .data .rodata           │
0x0001_0000 │ .text  (default entry)       │
            ├──────────────────────────────┤
0x0000_0000 │ null guard, NEVER mapped     │ 64 KiB
            └──────────────────────────────┘
```

**Why identity-map the kernel half?** The kernel runs at the same addresses before and after it sets
`PTBR.EN`, so turning paging on needs no trampoline code. Global (`G`) superpage mappings also mean the
kernel half costs only a few TLB entries and survives every context switch.

## 3. Bare-metal mode (milestones M1–M7, no OS)
Paging is off and programs run in Kernel mode. The assembler's default layout:

| Section | Address |
|---|---|
| `.text` | `0x8000_0000` |
| `.rodata`, `.data`, `.bss` | follow `.text`, page-aligned |
| stack top (`sp` at entry) | top of RAM, growing down |

The host-side loader places the program, sets `PC = entry` and `sp`, and starts the machine. The Boot ROM is
bypassed. A store to address 0 (for example `SW R3, 0(R0)`) raises a **StoreBusError**, because physical 0 is ROM.
