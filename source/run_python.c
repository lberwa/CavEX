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
#include <string.h>
#include <stdio.h>
#include "graphics/gfx_settings.h"
#include "version.h"

/* Letzter Python-Fehler (Datei:Zeile + Meldung) fuer die Anzeige im Menue.
   Leer, wenn init.py sauber lief. In allen Builds vorhanden. */
char g_py_error[160] = "";
bool g_py_error_show = false;

/* Basis-URL, von der der Loader (init.py / init_pc.py) nachgeladen wird, falls
   die lokale Datei fehlt. Struktur analog zum data_info-Download in init.py:
   /en/<plattform>/get/<datei>. Ueber -DCAVEX_INIT_URL_BASE=... ueberschreibbar. */
#ifndef CAVEX_INIT_URL_BASE
#define CAVEX_INIT_URL_BASE "https://192.168.15.188:5010"
#endif
#define CAVEX_INIT_URL_WII CAVEX_INIT_URL_BASE "/init_wii.py"
#define CAVEX_INIT_URL_PC CAVEX_INIT_URL_BASE  "/init_pc.py"

#ifdef WITH_PYTHON
#ifdef PLATFORM_WII

#include <gctypes.h>
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>

/* Grafik-Zeiger des Hosts (definiert in platform/wii/gfx.c). */
extern void *gfx_wii_screenmode(void);
extern void *gfx_wii_backbuffer(void);
/* Stellt CavEX' Video/GX nach einer Python-Uebernahme (rendering_adopt) wieder her. */
extern void gfx_wii_restore(void);

/* CavEX-Renderfunktionen (gui_util.c / platform/gfx.c). Ihre Adressen werden
   an init.py durchgereicht, damit Python sie ueber wiitools.c_run(addr, ...)
   direkt aufrufen kann. Nur forward-deklariert, um die Include-Kette (items.h
   usw.) hier nicht reinzuziehen; die Signaturen muessen mit gui_util.h /
   platform/gfx.h uebereinstimmen. */
struct tex_gfx;
extern void gfx_bind_texture(struct tex_gfx *tex);
extern void gutil_text(int x, int y, const char *str, int scale, bool shadow);
extern void gutil_texquad(int x, int y, int tx, int ty, int sx, int sy,
						  int width, int height);
extern void gutil_license(int width, int height);
extern int gutil_text_col(int col);
extern int gutil_font_width(const char *str, int scale);
/* GUI-GX-Zustand: gfx_mode_gui setzt Ortho-Projektion + CLR0=DIRECT (ohne das
   stallt der GP -> Haenger, weil gfx_draw_quads GX_Color4u8/DIRECT schreibt);
   gfx_texture(true) schaltet TEV auf MODULATE (Font-/Atlas-Textur sichtbar). */
extern void gfx_mode_gui(void);
extern void gfx_texture(bool enable);
/* weitere GUI-Helfer */
extern void gutil_window(int x, int y, int width, int height, char title[]);
extern void gutil_bg(void);
extern void gutil_bg_panorama(void);
extern void gfx_scissor(bool enable, uint32_t x, uint32_t y, uint32_t width,
						uint32_t height);
extern void gutil_texquad_col(int x, int y, int tx, int ty, int sx, int sy,
							  int width, int height, uint8_t r, uint8_t g,
							  uint8_t b, uint8_t a);
/* Setzt gstate.reboot/quit = true -> beendet die Hauptschleife und loest am
   Ende von main() einen Neustart (boot_dol) aus. */
extern void cavex_request_reboot(void);

/* Halbtransparente dunkle Box (wie der Abdunkel-Hintergrund in gutil_window).
   Eigene Funktion, weil gutil_texquad_col 12 Argumente hat und deshalb NICHT
   direkt ueber c_run (max. 8 Argumente) aufrufbar ist -- hier kapseln wir den
   Aufruf auf 4 Argumente (x, y, width, height). */
static void py_dim_box(int x, int y, int width, int height) {
	gfx_texture(false);
	gutil_texquad_col(x + 5, y + 5, 0, 0, 0, 0, width - 10, height - 10, 0, 0, 0,
					  180);
	gfx_texture(true);
}

/* CavEX-Textur-Globals (platform/texture.c). Ihre Adressen werden als Dict an
   init.py durchgereicht, damit Python vor gutil_texquad die passende Textur
   ueber gfx_bind_texture(&tex) binden kann. */
extern struct tex_gfx texture_terrain;
extern struct tex_gfx texture_terrain2;
extern struct tex_gfx texture_items;
extern struct tex_gfx texture_font;
extern struct tex_gfx texture_anim;
extern struct tex_gfx texture_gui_inventory;
extern struct tex_gfx texture_gui_crafting;
extern struct tex_gfx texture_gui_furnace;
extern struct tex_gfx texture_gui2;
extern struct tex_gfx texture_controls;
extern struct tex_gfx texture_pointer;

/* Baut ein dict { "name": adresse, ... } der Textur-Globals. */
static PyObject *make_tex_dict(void) {
	PyObject *d = PyDict_New();
	if(!d)
		return NULL;
#define ADD_TEX(key, sym)                                                      \
	do {                                                                       \
		PyObject *v = PyLong_FromUnsignedLong((unsigned long)&sym);            \
		if(v) {                                                                \
			PyDict_SetItemString(d, key, v);                                   \
			Py_DECREF(v);                                                      \
		}                                                                      \
	} while(0)
	ADD_TEX("terrain", texture_terrain);
	ADD_TEX("terrain2", texture_terrain2);
	ADD_TEX("items", texture_items);
	ADD_TEX("font", texture_font);
	ADD_TEX("anim", texture_anim);
	ADD_TEX("gui_inventory", texture_gui_inventory);
	ADD_TEX("gui_crafting", texture_gui_crafting);
	ADD_TEX("gui_furnace", texture_gui_furnace);
	ADD_TEX("gui2", texture_gui2);
	ADD_TEX("controls", texture_controls);
	ADD_TEX("pointer", texture_pointer);
#undef ADD_TEX
	return d;
}

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

void cavex_run_python_file(const char *path, const char *arg) {
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

	/* Adressen der CavEX-Renderfunktionen als Integer bereitstellen. */
	set_main_global("_hbc_gfx_bind_texture",
					PyLong_FromUnsignedLong((unsigned long)&gfx_bind_texture));
	set_main_global("_hbc_gutil_text",
					PyLong_FromUnsignedLong((unsigned long)&gutil_text));
	set_main_global("_hbc_gutil_texquad",
					PyLong_FromUnsignedLong((unsigned long)&gutil_texquad));
	set_main_global("_hbc_gutil_license",
					PyLong_FromUnsignedLong((unsigned long)&gutil_license));
	set_main_global("_hbc_gutil_text_col",
					PyLong_FromUnsignedLong((unsigned long)&gutil_text_col));
	set_main_global("_hbc_gfx_mode_gui",
					PyLong_FromUnsignedLong((unsigned long)&gfx_mode_gui));
	set_main_global("_hbc_gfx_texture",
					PyLong_FromUnsignedLong((unsigned long)&gfx_texture));
	set_main_global("_hbc_gutil_window",
					PyLong_FromUnsignedLong((unsigned long)&gutil_window));
	set_main_global("_hbc_gutil_bg",
					PyLong_FromUnsignedLong((unsigned long)&gutil_bg));
	set_main_global("_hbc_gutil_bg_panorama",
					PyLong_FromUnsignedLong((unsigned long)&gutil_bg_panorama));
	set_main_global("_hbc_gfx_scissor",
					PyLong_FromUnsignedLong((unsigned long)&gfx_scissor));
	set_main_global("_hbc_dim_box",
					PyLong_FromUnsignedLong((unsigned long)&py_dim_box));
	set_main_global("_hbc_gutil_font_width",
					PyLong_FromUnsignedLong((unsigned long)&gutil_font_width));
	set_main_global("_hbc_reboot",
					PyLong_FromUnsignedLong((unsigned long)&cavex_request_reboot));

	/* Textur-Globals als dict {name: adresse}. */
	set_main_global("_hbc_tex", make_tex_dict());

	/* URL, von der init.py nachgeladen wird, falls die lokale Datei fehlt. */
	set_main_global("_hbc_init_url", PyUnicode_FromString(CAVEX_INIT_URL_WII));

	/* Startup-Argument (z.B. "no_resources") -> wird an init.py durchgereicht,
	   die es wiederum an main.py weitergibt. Leerer String, wenn keins. */
	set_main_global("_hbc_arg", PyUnicode_FromString(arg ? arg : ""));

	/* Versions-/Plattform-Infos (aus version.h) fuer die Python-Seite. */
	{
		char ver[64];
		snprintf(ver, sizeof(ver), "Alpha %i.%i.%i_f%i (impl. %s)",
				 VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_FORK,
				 VERSION_IMPL);
		set_main_global("_hbc_game_name", PyUnicode_FromString(GAME_NAME));
		set_main_global("_hbc_version_major", PyLong_FromLong(VERSION_MAJOR));
		set_main_global("_hbc_version_minor", PyLong_FromLong(VERSION_MINOR));
		set_main_global("_hbc_version_patch", PyLong_FromLong(VERSION_PATCH));
		set_main_global("_hbc_version_fork", PyLong_FromLong(VERSION_FORK));
		set_main_global("_hbc_version_impl", PyUnicode_FromString(VERSION_IMPL));
		set_main_global("_hbc_version", PyUnicode_FromString(ver));
		set_main_global("_hbc_platform", PyUnicode_FromString("wii"));
		set_main_global("_hbc_license", PyUnicode_FromString(LICENSE));
		set_main_global("_hbc_copyright", PyUnicode_FromString(COPYRIGHT));
	}

	/* Fehlt die lokale init.py? -> ueber wiitools/curl von _hbc_init_url
	   nachladen und unter _hbc_path ablegen (analog zum data_info-Download in
	   init.py). Netzwerk wird per net_init()/IsNetReady() hochgefahren; das
	   ganze Vorgehen ist in try/except gekapselt, damit ein Download-Fehler die
	   spaetere runpy-Fehleranzeige nicht verschluckt (dann greift dort einfach
	   der FileNotFoundError). */
	PyRun_SimpleString(
		"import os\n"
		"if not os.path.exists(_hbc_path):\n"
		"    try:\n"
		"        import wiitools as _w\n"
		"        _w.rendering_adopt(_hbc_mode, _hbc_fb)\n"
		"        def _msg(_t):\n"
		"            _w.draw_rect(0, 0, _w.width, _w.height, (255, 255, 255, 255), 0)\n"
		"            _w.render_text(int(_w.width/2 - _w.text_length(_t, 2)),\n"
		"                           int(_w.height/2 - 10), _t, 2, True, (0, 0, 255, 255), 0)\n"
		"            _w.update()\n"
		"        _msg('downloading init.py ...')\n"
		"        _w.net_init()\n"
		"        _tries = 0\n"
		"        while not _w.IsNetReady() and _tries < 10000:\n"
		"            _msg('waiting for network ...')\n"
		"            _w.usleep(1000)\n"
		"            _tries += 1\n"
		"        _r = _w.curl_get(_hbc_init_url)\n"
		"        if _r['ok']:\n"
		"            with open(_hbc_path, 'wb') as _f:\n"
		"                _f.write(_r['body'])\n"
		"        else:\n"
		"            _msg('download failed')\n"
		"    except BaseException:\n"
		"        pass\n");

	/* runpy in try/except: faengt AUCH SystemExit ab (sonst wuerde exit() den
	   ganzen Prozess beenden). Bei einer Exception wird _err mit der letzten
	   init.py-Zeile + Fehlertyp/Meldung befuellt (fuer die Anzeige im Menue). */
	PyRun_SimpleString(
		"import runpy, sys\n"
		"_err = ''\n"
		"try:\n"
		"    if _hbc_dir and _hbc_dir not in sys.path:\n"
		"        sys.path.insert(0, _hbc_dir)\n"
		"    runpy.run_path(_hbc_path,\n"
		"                   init_globals={'mode_ptr': _hbc_mode, 'fb_ptr': _hbc_fb,\n"
		"                                 'gfx_bind_texture_ptr': _hbc_gfx_bind_texture,\n"
		"                                 'gutil_text_ptr': _hbc_gutil_text,\n"
		"                                 'gutil_texquad_ptr': _hbc_gutil_texquad,\n"
		"                                 'gutil_license_ptr': _hbc_gutil_license,\n"
		"                                 'gutil_text_col_ptr': _hbc_gutil_text_col,\n"
		"                                 'gfx_mode_gui_ptr': _hbc_gfx_mode_gui,\n"
		"                                 'gfx_texture_ptr': _hbc_gfx_texture,\n"
		"                                 'gutil_window_ptr': _hbc_gutil_window,\n"
		"                                 'gutil_bg_ptr': _hbc_gutil_bg,\n"
		"                                 'gutil_bg_panorama_ptr': _hbc_gutil_bg_panorama,\n"
		"                                 'gfx_scissor_ptr': _hbc_gfx_scissor,\n"
		"                                 'dim_box_ptr': _hbc_dim_box,\n"
		"                                 'gutil_font_width_ptr': _hbc_gutil_font_width,\n"
		"                                 'reboot_ptr': _hbc_reboot,\n"
		"                                 'tex_ptrs': _hbc_tex,\n"
		"                                 'startup_arg': _hbc_arg,\n"
		"                                 'game_name': _hbc_game_name,\n"
		"                                 'version_major': _hbc_version_major,\n"
		"                                 'version_minor': _hbc_version_minor,\n"
		"                                 'version_patch': _hbc_version_patch,\n"
		"                                 'version_fork': _hbc_version_fork,\n"
		"                                 'version_impl': _hbc_version_impl,\n"
		"                                 'version': _hbc_version,\n"
		"                                 'platform': _hbc_platform,\n"
		"                                 'license': _hbc_license,\n"
		"                                 'copyright': _hbc_copyright},\n"
		"                   run_name='__main__')\n"
		"except SystemExit:\n"
		"    _err = ''\n"
		"except BaseException as e:\n"
		"    _t = e.__traceback__\n"
		"    _ln = -1\n"
		"    while _t is not None:\n"
		"        if _t.tb_frame.f_code.co_filename.endswith('init.py'):\n"
		"            _ln = _t.tb_lineno\n"
		"        _t = _t.tb_next\n"
		"    _err = 'init.py:' + str(_ln) + ' ' + type(e).__name__ + ': ' + str(e)\n");

	/* _err aus __main__ holen (noch VOR Py_Finalize, solange Objekte gueltig). */
	{
		PyObject *m = PyImport_AddModule("__main__");       /* borrowed */
		PyObject *g = m ? PyModule_GetDict(m) : NULL;       /* borrowed */
		PyObject *e = g ? PyDict_GetItemString(g, "_err") : NULL; /* borrowed */
		const char *msg = e ? PyUnicode_AsUTF8(e) : NULL;
		if(msg && msg[0]) {
			strncpy(g_py_error, msg, sizeof(g_py_error) - 1);
			g_py_error[sizeof(g_py_error) - 1] = '\0';
			g_py_error_show = true;
		} else {
			g_py_error[0] = '\0';
			g_py_error_show = false;
		}
	}

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

#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CavEX-Funktionen, deren Adressen an init_pc.py durchgereicht werden. init_pc.py
   ruft sie ueber ctypes anhand dieser Adressen auf (auf PC gibt es kein c_run --
   c_run ist Wii-spezifisch). Gleiche Symbole wie im Wii-Block, hier fuer den
   PC-Build erneut deklariert. */
struct tex_gfx;
extern void gfx_bind_texture(struct tex_gfx *tex);
extern void gutil_text(int x, int y, const char *str, int scale, bool shadow);
extern void gutil_texquad(int x, int y, int tx, int ty, int sx, int sy,
						  int width, int height);
extern void gutil_license(int width, int height);
extern int gutil_text_col(int col);
extern int gutil_font_width(const char *str, int scale);
extern void gfx_mode_gui(void);
extern void gfx_texture(bool enable);
extern void gutil_window(int x, int y, int width, int height, char title[]);
extern void gutil_bg(void);
extern void gutil_bg_panorama(void);
extern void gfx_scissor(bool enable, uint32_t x, uint32_t y, uint32_t width,
						uint32_t height);
extern void gutil_texquad_col(int x, int y, int tx, int ty, int sx, int sy,
							  int width, int height, uint8_t r, uint8_t g,
							  uint8_t b, uint8_t a);
/* PC-spezifisch: Bildschirm loeschen, Frame praesentieren (swap+poll),
   Groesse abfragen, Input pollen/lesen. */
extern void gfx_clear_buffers(uint8_t r, uint8_t g, uint8_t b);
extern void gfx_finish(bool vsync);
extern int gfx_width(void);
extern int gfx_height(void);
extern void input_poll(void);
extern bool input_pressed(int b, int player); /* enum input_button == int */
extern bool input_released(int b, int player);
extern bool input_held(int b, int player);
extern void gfx_pointer_gui(int *x, int *y); /* Maus in logischen GUI-Koords */
/* Setzt gstate.reboot/quit = true -> beendet die Hauptschleife und loest am
   Ende von main() einen Neustart (boot_dol) aus. */
extern void cavex_request_reboot(void);

/* Textur-Globals (platform/texture.c). */
extern struct tex_gfx texture_terrain;
extern struct tex_gfx texture_terrain2;
extern struct tex_gfx texture_items;
extern struct tex_gfx texture_font;
extern struct tex_gfx texture_anim;
extern struct tex_gfx texture_gui_inventory;
extern struct tex_gfx texture_gui_crafting;
extern struct tex_gfx texture_gui_furnace;
extern struct tex_gfx texture_gui2;
extern struct tex_gfx texture_controls;
extern struct tex_gfx texture_pointer;

/* Halbtransparente dunkle Box (kapselt das 12-Arg gutil_texquad_col). */
static void py_dim_box(int x, int y, int width, int height) {
	gfx_texture(false);
	gutil_texquad_col(x + 5, y + 5, 0, 0, 0, 0, width - 10, height - 10, 0, 0, 0,
					  180);
	gfx_texture(true);
}

static PyObject *make_tex_dict(void) {
	PyObject *d = PyDict_New();
	if(!d)
		return NULL;
#define ADD_TEX(key, sym)                                                      \
	do {                                                                       \
		PyObject *v = PyLong_FromUnsignedLongLong((unsigned long long)(uintptr_t)&sym); \
		if(v) {                                                                \
			PyDict_SetItemString(d, key, v);                                   \
			Py_DECREF(v);                                                      \
		}                                                                      \
	} while(0)
	ADD_TEX("terrain", texture_terrain);
	ADD_TEX("terrain2", texture_terrain2);
	ADD_TEX("items", texture_items);
	ADD_TEX("font", texture_font);
	ADD_TEX("anim", texture_anim);
	ADD_TEX("gui_inventory", texture_gui_inventory);
	ADD_TEX("gui_crafting", texture_gui_crafting);
	ADD_TEX("gui_furnace", texture_gui_furnace);
	ADD_TEX("gui2", texture_gui2);
	ADD_TEX("controls", texture_controls);
	ADD_TEX("pointer", texture_pointer);
#undef ADD_TEX
	return d;
}

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

/* Adresse einer Funktion als Python-int (64-bit-tauglich fuer x86-64). */
#define PY_ADDR(fn) PyLong_FromUnsignedLongLong((unsigned long long)(uintptr_t)(fn))

void cavex_run_python_file(const char *path, const char *arg) {
	Py_Initialize();

	/* Skriptverzeichnis (alles vor dem letzten '/') fuer sys.path. */
	char dir[512];
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

	set_main_global("_hbc_path", PyUnicode_FromString(path));
	set_main_global("_hbc_dir", PyUnicode_FromString(dir));

	/* Funktionsadressen als Integer. */
	set_main_global("_hbc_gfx_bind_texture", PY_ADDR(&gfx_bind_texture));
	set_main_global("_hbc_gutil_text", PY_ADDR(&gutil_text));
	set_main_global("_hbc_gutil_texquad", PY_ADDR(&gutil_texquad));
	set_main_global("_hbc_gutil_license", PY_ADDR(&gutil_license));
	set_main_global("_hbc_gutil_text_col", PY_ADDR(&gutil_text_col));
	set_main_global("_hbc_gfx_mode_gui", PY_ADDR(&gfx_mode_gui));
	set_main_global("_hbc_gfx_texture", PY_ADDR(&gfx_texture));
	set_main_global("_hbc_gutil_window", PY_ADDR(&gutil_window));
	set_main_global("_hbc_gutil_bg", PY_ADDR(&gutil_bg));
	set_main_global("_hbc_gutil_bg_panorama", PY_ADDR(&gutil_bg_panorama));
	set_main_global("_hbc_gfx_scissor", PY_ADDR(&gfx_scissor));
	set_main_global("_hbc_dim_box", PY_ADDR(&py_dim_box));
	set_main_global("_hbc_gutil_font_width", PY_ADDR(&gutil_font_width));
	set_main_global("_hbc_gfx_clear", PY_ADDR(&gfx_clear_buffers));
	set_main_global("_hbc_gfx_finish", PY_ADDR(&gfx_finish));
	set_main_global("_hbc_gfx_width", PY_ADDR(&gfx_width));
	set_main_global("_hbc_gfx_height", PY_ADDR(&gfx_height));
	set_main_global("_hbc_input_poll", PY_ADDR(&input_poll));
	set_main_global("_hbc_input_pressed", PY_ADDR(&input_pressed));
	set_main_global("_hbc_input_released", PY_ADDR(&input_released));
	set_main_global("_hbc_input_held", PY_ADDR(&input_held));
	set_main_global("_hbc_pointer_gui", PY_ADDR(&gfx_pointer_gui));
	set_main_global("_hbc_reboot", PY_ADDR(&cavex_request_reboot));
	set_main_global("_hbc_tex", make_tex_dict());

	/* URL, von der init_pc.py nachgeladen wird, falls die lokale Datei fehlt. */
	set_main_global("_hbc_init_url", PyUnicode_FromString(CAVEX_INIT_URL_PC));

	/* Startup-Argument (z.B. "no_resources") -> an init_pc.py durchgereicht,
	   die es an main.py weitergibt. Leerer String, wenn keins. */
	set_main_global("_hbc_arg", PyUnicode_FromString(arg ? arg : ""));

	/* Versions-/Plattform-Infos (aus version.h) fuer die Python-Seite. */
	{
		char ver[64];
		snprintf(ver, sizeof(ver), "Alpha %i.%i.%i_f%i (impl. %s)",
				 VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_FORK,
				 VERSION_IMPL);
		set_main_global("_hbc_game_name", PyUnicode_FromString(GAME_NAME));
		set_main_global("_hbc_version_major", PyLong_FromLong(VERSION_MAJOR));
		set_main_global("_hbc_version_minor", PyLong_FromLong(VERSION_MINOR));
		set_main_global("_hbc_version_patch", PyLong_FromLong(VERSION_PATCH));
		set_main_global("_hbc_version_fork", PyLong_FromLong(VERSION_FORK));
		set_main_global("_hbc_version_impl", PyUnicode_FromString(VERSION_IMPL));
		set_main_global("_hbc_version", PyUnicode_FromString(ver));
		set_main_global("_hbc_platform", PyUnicode_FromString("pc"));
		set_main_global("_hbc_license", PyUnicode_FromString(LICENSE));
		set_main_global("_hbc_copyright", PyUnicode_FromString(COPYRIGHT));
	}

	/* Fehlt die lokale init_pc.py? -> ueber urllib von _hbc_init_url nachladen
	   und unter _hbc_path ablegen. HTTPS ohne Zertifikatspruefung (self-signed
	   Server), analog zu http_get() in init_pc.py. In try/except gekapselt,
	   damit ein Download-Fehler die runpy-Fehleranzeige nicht verschluckt. */
	PyRun_SimpleString(
		"import os\n"
		"if not os.path.exists(_hbc_path):\n"
		"    try:\n"
		"        import ssl, urllib.request\n"
		"        _ctx = ssl.create_default_context()\n"
		"        _ctx.check_hostname = False\n"
		"        _ctx.verify_mode = ssl.CERT_NONE\n"
		"        _req = urllib.request.Request(_hbc_init_url,\n"
		"                                      headers={'User-Agent': 'ffCavEX-PC'})\n"
		"        with urllib.request.urlopen(_req, timeout=30, context=_ctx) as _r:\n"
		"            _data = _r.read()\n"
		"        with open(_hbc_path, 'wb') as _f:\n"
		"            _f.write(_data)\n"
		"    except BaseException:\n"
		"        pass\n");

	PyRun_SimpleString(
		"import runpy, sys\n"
		"_err = ''\n"
		"try:\n"
		"    if _hbc_dir and _hbc_dir not in sys.path:\n"
		"        sys.path.insert(0, _hbc_dir)\n"
		"    runpy.run_path(_hbc_path,\n"
		"                   init_globals={'gfx_bind_texture_ptr': _hbc_gfx_bind_texture,\n"
		"                                 'gutil_text_ptr': _hbc_gutil_text,\n"
		"                                 'gutil_texquad_ptr': _hbc_gutil_texquad,\n"
		"                                 'gutil_license_ptr': _hbc_gutil_license,\n"
		"                                 'gutil_text_col_ptr': _hbc_gutil_text_col,\n"
		"                                 'gfx_mode_gui_ptr': _hbc_gfx_mode_gui,\n"
		"                                 'gfx_texture_ptr': _hbc_gfx_texture,\n"
		"                                 'gutil_window_ptr': _hbc_gutil_window,\n"
		"                                 'gutil_bg_ptr': _hbc_gutil_bg,\n"
		"                                 'gutil_bg_panorama_ptr': _hbc_gutil_bg_panorama,\n"
		"                                 'gfx_scissor_ptr': _hbc_gfx_scissor,\n"
		"                                 'dim_box_ptr': _hbc_dim_box,\n"
		"                                 'gutil_font_width_ptr': _hbc_gutil_font_width,\n"
		"                                 'gfx_clear_ptr': _hbc_gfx_clear,\n"
		"                                 'gfx_finish_ptr': _hbc_gfx_finish,\n"
		"                                 'gfx_width_ptr': _hbc_gfx_width,\n"
		"                                 'gfx_height_ptr': _hbc_gfx_height,\n"
		"                                 'input_poll_ptr': _hbc_input_poll,\n"
		"                                 'input_pressed_ptr': _hbc_input_pressed,\n"
		"                                 'input_released_ptr': _hbc_input_released,\n"
		"                                 'input_held_ptr': _hbc_input_held,\n"
		"                                 'pointer_gui_ptr': _hbc_pointer_gui,\n"
		"                                 'reboot_ptr': _hbc_reboot,\n"
		"                                 'tex_ptrs': _hbc_tex,\n"
		"                                 'startup_arg': _hbc_arg,\n"
		"                                 'game_name': _hbc_game_name,\n"
		"                                 'version_major': _hbc_version_major,\n"
		"                                 'version_minor': _hbc_version_minor,\n"
		"                                 'version_patch': _hbc_version_patch,\n"
		"                                 'version_fork': _hbc_version_fork,\n"
		"                                 'version_impl': _hbc_version_impl,\n"
		"                                 'version': _hbc_version,\n"
		"                                 'platform': _hbc_platform,\n"
		"                                 'license': _hbc_license,\n"
		"                                 'copyright': _hbc_copyright},\n"
		"                   run_name='__main__')\n"
		"except SystemExit:\n"
		"    _err = ''\n"
		"except BaseException as e:\n"
		"    _t = e.__traceback__\n"
		"    _ln = -1\n"
		"    while _t is not None:\n"
		"        if _t.tb_frame.f_code.co_filename.endswith('init_pc.py'):\n"
		"            _ln = _t.tb_lineno\n"
		"        _t = _t.tb_next\n"
		"    _err = 'init_pc.py:' + str(_ln) + ' ' + type(e).__name__ + ': ' + str(e)\n");

	{
		PyObject *m = PyImport_AddModule("__main__");
		PyObject *g = m ? PyModule_GetDict(m) : NULL;
		PyObject *e = g ? PyDict_GetItemString(g, "_err") : NULL;
		const char *msg = e ? PyUnicode_AsUTF8(e) : NULL;
		if(msg && msg[0]) {
			strncpy(g_py_error, msg, sizeof(g_py_error) - 1);
			g_py_error[sizeof(g_py_error) - 1] = '\0';
			g_py_error_show = true;
		} else {
			g_py_error[0] = '\0';
			g_py_error_show = false;
		}
	}

	Py_Finalize();
}

#ifdef RUN_PY_SELFTEST
void cavex_python_selftest(void) {
}
#endif

#endif /* PLATFORM_PC */

/////////////////////////////////////////////////////////////////////

#else /* !WITH_PYTHON */

void cavex_run_python_file(const char *path, const char *arg) {
	(void)path;
	(void)arg;
}

#ifdef RUN_PY_SELFTEST
void cavex_python_selftest(void) {
}
#endif

#endif
