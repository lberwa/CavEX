/*
	CPython, direkt in CavEX gelinkt (ein Binary, kein Overlay).

	- cavex_run_python_file(path): startet CPython, reicht mode_ptr/fb_ptr aus
	  CavEX' Grafik durch und fuehrt die Skriptdatei aus (z.B. ./init.py).
	  Wird vom "Server"-Menuepunkt aufgerufen.
	- cavex_python_selftest(): optionaler Selbsttest (nur wenn RUN_PY_SELFTEST).

	Ohne WITH_PYTHON sind beide leer -> keine Python-Abhaengigkeit.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <gctypes.h>
#include "graphics/gfx_settings.h"

#ifdef WITH_PYTHON
#ifdef PLATFORM_WII

#include <Python.h>
#include <stdio.h>
#include <stdlib.h>

/* Grafik-Zeiger des Hosts (definiert in platform/wii/gfx.c). */
extern void *gfx_wii_screenmode(void);
extern void *gfx_wii_backbuffer(void);
/* Stellt CavEX' Video/GX nach einer Python-Uebernahme (rendering_adopt) wieder her. */
extern void gfx_wii_restore(void);

/* Setzt eine Variable im __main__-Namensraum. */
static void set_main_global(const char *name, PyObject *value /* stolen */) {
	if(!value)
		return;
	PyObject *m = PyImport_AddModule("__main__"); /* borrowed */
	if(m) {
		PyObject *g = PyModule_GetDict(m); /* borrowed */
		PyDict_SetItemString(g, name, value);
	}
	Py_DECREF(value);
}

/* Fuehrt eine Python-Datei via runpy.run_path als __main__ aus.

   runpy.run_path startet das Skript in einem FRISCHEN Namespace -- deshalb
   kommen mode_ptr/fb_ptr ueber init_globals rein (nicht ueber __main__, das
   waere im Skript nicht sichtbar). Verzeichnis des Skripts kommt auf sys.path,
   damit Nachbar-Module importierbar sind.

   Voraussetzung: die Stdlib (runpy + Abhaengigkeiten) muss unter den
   Py_Init_Custom-Pfaden liegen (usb:/python bzw. sd:/python). */
extern void sdlog(const char *msg);
extern volatile bool g_python_running; /* Trace-Thread darf waehrenddessen kein FAT anfassen */

void cavex_run_python_file(const char *path) {
	g_python_running = true;
	sdlog("py: vor Py_Init_Custom");
	size_t count = 2;
	PyStatus status = Py_Init_Custom(
		(const char *[]) {"usb:/python", "sd:/python"}, &count);
	if(status._type != _PyStatus_TYPE_OK) {
		sdlog("py: Py_Init_Custom FEHLGESCHLAGEN");
		return;
	}
	sdlog("py: Py_Init_Custom OK");

	/* stdout/stderr auf No-op umlenken. In diesem Build zeigen sie sonst auf
	   terminal_print -> render_text_ohne_bild, und das dereferenziert bei
	   nicht initialisiertem Terminal einen NULL-Framebuffer (Crash). Betrifft
	   NICHT nur print(), sondern vor allem den automatischen Traceback-Druck
	   bei einer Exception. So bleiben Python-Fehler harmlos abgefangen. */
	PyRun_SimpleString(
		"import sys\n"
		"class _Null:\n"
		"    def write(self, *a): return 0\n"
		"    def flush(self): pass\n"
		"sys.stdout = sys.stderr = _Null()\n");

	/* Skriptverzeichnis aus dem Pfad ableiten (alles vor dem letzten '/'). */
	char dir[256];
	dir[0] = '\0';
	{
		const char *slash = NULL;
		for(const char *p = path; *p; ++p)
			if(*p == '/')
				slash = p;
		if(slash && (size_t)(slash - path) < sizeof(dir)) {
			size_t n = (size_t)(slash - path);
			memcpy(dir, path, n);
			dir[n] = '\0';
		}
	}

	/* Werte als __main__-Globals bereitstellen -> kein String-Escaping noetig. */
	set_main_global("_hbc_path", PyUnicode_FromString(path));
	set_main_global("_hbc_dir", PyUnicode_FromString(dir));
	set_main_global("_hbc_mode",
					PyLong_FromUnsignedLong((unsigned long)gfx_wii_screenmode()));
	set_main_global("_hbc_fb",
					PyLong_FromUnsignedLong((unsigned long)gfx_wii_backbuffer()));

	/* runpy in try/except: faengt AUCH SystemExit ab, sonst wuerde exit()/exit(0)
	   in init.py den ganzen Prozess beenden. */
	PyRun_SimpleString(
		"import runpy, sys\n"
		"try:\n"
		"    if _hbc_dir and _hbc_dir not in sys.path:\n"
		"        sys.path.insert(0, _hbc_dir)\n"
		"    runpy.run_path(_hbc_path,\n"
		"                   init_globals={'mode_ptr': _hbc_mode, 'fb_ptr': _hbc_fb},\n"
		"                   run_name='__main__')\n"
		"except SystemExit:\n"
		"    pass\n"
		"except BaseException:\n"
		"    pass\n");

	sdlog("py: nach runpy, vor Py_Finalize");
	Py_Finalize();
	sdlog("py: nach Py_Finalize");

	/* Python (rendering_adopt) hat Video/GX uebernommen -> CavEX-Grafik
	   wiederherstellen, sonst bleibt der Bildschirm auf dem letzten Python-
	   Frame stehen und das Menue ist unsichtbar. */
	gfx_wii_restore();
	sdlog("py: nach gfx_wii_restore");
	g_python_running = false; /* ab jetzt darf der Trace-Thread flushen */
}

#ifdef RUN_PY_SELFTEST
void cavex_python_selftest(void) {
	size_t count = 3;
	PyStatus status = Py_Init_Custom(
		(const char *[]) {"sd:/", "sd:", "sd:/python"}, &count);
	if(status._type != _PyStatus_TYPE_OK)
		return;
	/* KEIN print() -- Pythons stdout geht auf terminal_print (Video) und
	   crasht ohne initialisiertes Terminal. Nur rechnen. */
	if(PyRun_SimpleString("_r = sum(i*i for i in range(1000))\n") != 0)
		PyErr_Clear();
	Py_Finalize();
}
#endif

#endif /* PLATFORM_WII */

/////////////////////////////////////////////////////////////////////

#ifdef PLATFORM_PC

#include <stdio.h>
#include <stdlib.h>

void cavex_run_python_file(const char *path) {
	char buf[128];
	sprintf(buf, "bash -c 'python3 %s'", path);

    system(buf);

    return 0;
}

#ifdef RUN_PY_SELFTEST
void cavex_python_selftest(void) {
}
#endif

#endif /* PLATFORM_PC */

/////////////////////////////////////////////////////////////////////

#else /* !WITH_PYTHON */

void cavex_run_python_file(const char *path) {
	(void)path;
}

#ifdef RUN_PY_SELFTEST
void cavex_python_selftest(void) {
}
#endif

#endif
