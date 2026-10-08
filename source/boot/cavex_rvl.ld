/*--------------------------------------------------------------------------------
	CavEX: Ergaenzungs-Linkerskript (MY=0, HBC-kompatibler Boot)

	WICHTIG: Dieses Skript enthaelt BEWUSST KEIN SECTIONS/PHDRS. Der gcc-Treiber
	uebergibt via -mrvl-Specs bereits das Default-"rvl.ld" an ld; dieses Skript
	wird als ZWEITES -T zusaetzlich verarbeitet. Haette es ein eigenes
	SECTIONS/PHDRS, wuerde ld das komplette Layout ein zweites Mal anlegen ->
	jeder ladbare Abschnitt landete DOPPELT in der .dol (2x Groesse, 8 statt 4
	LOAD-Segmente).

	Es definiert nur die Symbole nach, die aeltere devkitPPC-rvl.ld lieferte,
	die release 46 aber entfernt hat und die die gebuendelten libogc-2.12.0-
	Objekte (source/boot/system.o, lib/wii/libogc.a) noch brauchen, plus
	ENTRY(_start) (ogc_crt0.o) statt __app_start.
--------------------------------------------------------------------------------*/

ENTRY(_start)

/* Alter MEM1-Arena-Deckel: reserviert den Top-Bereich fuer __gxregs
   (release-46-rvl.ld setzt 0x81800000). Ueberschreibt dessen PROVIDE_HIDDEN. */
__Arena1Hi       = 0x817FEFF0;

__stack_end      = (__bss_start + SIZEOF(.bss));
__stack_addr     = (__bss_start + SIZEOF(.bss) + 0x20000 + 7) & (-8);
__intrstack_end  = (__stack_addr);
__intrstack_addr = (__stack_addr + 0x4000);
__gxregs         = (__Arena1Hi + 31) & (-32);

/* WICHTIG: release-46 libogc_common.ld setzt __Arena1Lo direkt hinter .bss.
   Dann laege der oben definierte Main-/Interrupt-Stack MITTEN in der Arena ->
   libogc ueberschreibt beim Allokieren den Stack -> Crash in genulltem Speicher.
   Wie die alte rvl.ld die Arena ERST OBERHALB des Interrupt-Stacks beginnen
   lassen (Reihenfolge: bss | stack | intrstack | Arena1Lo..Arena1Hi). */
__Arena1Lo       = (__intrstack_addr + 31) & (-32);

PROVIDE(__stack_addr     = __stack_addr);
PROVIDE(__stack_end      = __stack_end);
PROVIDE(__intrstack_addr = __intrstack_addr);
PROVIDE(__intrstack_end  = __intrstack_end);
PROVIDE(__Arena1Lo       = __Arena1Lo);
PROVIDE(__gxregs         = __gxregs);
