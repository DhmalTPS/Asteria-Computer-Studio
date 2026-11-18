# Documentation

The docs are the **source of truth**. Code implements them. When a design changes, the doc changes first.

## Reading order
1. [DECISIONS.md](DECISIONS.md): every cross-cutting design decision and why it was made.
2. [architecture/OVERVIEW.md](architecture/OVERVIEW.md): repository layout, module dependency rules,
   and one load traced end to end.
3. [isa/ASTERIA-32.md](isa/ASTERIA-32.md): the instruction set, normative.
4. [platform/MEMORY-MAP.md](platform/MEMORY-MAP.md): physical and virtual address spaces.
5. [ROADMAP.md](ROADMAP.md): milestones and what "done" means.
6. [architecture/CODING-STYLE.md](architecture/CODING-STYLE.md): conventions every file follows.
7. [references/README.md](references/README.md): reviews of five external projects, what Asteria
   adopts from them, and what it avoids.
8. `study/`: one note per finished milestone, a design walkthrough plus an interview quiz.
   Start with [study/M00-foundation.md](study/M00-foundation.md).

## Planned
- `formats/AXF.md`: object/executable container (M1 subset, full in M9)
- `platform/DEVICES.md`: device register layouts (M8)
- `formats/DISK.md`: filesystem on-disk format (M11)
