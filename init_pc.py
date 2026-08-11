# PC-Loader (PLATFORM_PC). Anders als init.py (Wii) laeuft hier CPython
# in-process in CavEX (run_python.c, #ifdef PLATFORM_PC) und ruft die
# CavEX-Funktionen NICHT ueber wiitools.c_run, sondern ueber ctypes anhand der
# Adressen auf, die run_python.c per init_globals uebergibt. HTTP laeuft ueber
# urllib (volle PC-Stdlib), nicht ueber w.curl_get.
gfx = None
try:
    import ctypes
    import json
    import runpy
    import os
    import ssl
    import urllib.request

    # ------------------------------------------------------------------
    # ctypes-Wrapper um die CavEX-Render-/Input-Funktionen. Die Adressen
    # (als int) kommen aus run_python.c. CFUNCTYPE(restype, *argtypes)(addr)
    # erzeugt eine aufrufbare Funktion an dieser Adresse.
    # ------------------------------------------------------------------
    class CavexGfx:
        def __init__(self, ptrs, tex):
            C = ctypes.CFUNCTYPE
            ci, cc, cv, cu = (ctypes.c_int, ctypes.c_char_p,
                              ctypes.c_void_p, ctypes.c_uint)
            cb = ctypes.c_ubyte
            # Render / gutil
            self._bind_texture = C(None, cv)(ptrs["bind_texture"])
            self._text = C(None, ci, ci, cc, ci, ci)(ptrs["text"])
            self._texquad = C(None, ci, ci, ci, ci, ci, ci, ci, ci)(ptrs["texquad"])
            self._license = C(None, ci, ci)(ptrs["license"])
            self._text_col = C(ci, ci)(ptrs["text_col"])
            self._font_width = C(ci, cc, ci)(ptrs["font_width"])
            self._mode_gui = C(None)(ptrs["mode_gui"])
            self._texture = C(None, ci)(ptrs["texture"])
            self._window = C(None, ci, ci, ci, ci, cc)(ptrs["window"])
            self._bg = C(None)(ptrs["bg"])
            self._bg_panorama = C(None)(ptrs["bg_panorama"])
            self._scissor = C(None, ci, cu, cu, cu, cu)(ptrs["scissor"])
            self._dim_box = C(None, ci, ci, ci, ci)(ptrs["dim_box"])
            # PC: Bildschirm/Frame/Input
            self._clear = C(None, cb, cb, cb)(ptrs["clear"])
            self._finish = C(None, ci)(ptrs["finish"])
            self._width = C(ci)(ptrs["width"])
            self._height = C(ci)(ptrs["height"])
            self._input_poll = C(None)(ptrs["input_poll"])
            self._input_pressed = C(ctypes.c_bool, ci, ci)(ptrs["input_pressed"])
            self._input_released = C(ctypes.c_bool, ci, ci)(ptrs["input_released"])
            self._input_held = C(ctypes.c_bool, ci, ci)(ptrs["input_held"])
            self._pointer_gui = C(None, ctypes.POINTER(ci),
                                  ctypes.POINTER(ci))(ptrs["pointer_gui"])
            self._reboot = C(None)(ptrs["reboot"])
            self.tex = tex

        # enum input_button (source/platform/input.h) -- Reihenfolge = Wert.
        # Erlaubt Buttons als String, z.B. gfx.input_pressed("IB_BACK").
        BUTTONS = {name: i for i, name in enumerate([
            "IB_FORWARD", "IB_BACKWARD", "IB_LEFT", "IB_RIGHT",
            "IB_ACTION1", "IB_ACTION2", "IB_JUMP", "IB_SNEAK",
            "IB_INVENTORY", "IB_HOME", "IB_SCROLL_LEFT", "IB_SCROLL_RIGHT",
            "IB_GUI_UP", "IB_GUI_DOWN", "IB_GUI_LEFT", "IB_GUI_RIGHT",
            "IB_GUI_CLICK", "IB_GUI_CLICK_ALT", "IB_SCREENSHOT", "IB_BACK",
            "IB_ANY",
        ])}

        # gutil_* nutzen CavEX' GUI-Zustand -> gfx_mode_gui() setzt Ortho +
        # GL-Zustand, gfx_texture(True) aktiviert Texturierung. (Auf PC gibt es
        # kein GX-Vertex-Desync-Problem wie auf der Wii.)
        def begin_gui(self):
            self._mode_gui()
            self._texture(1)

        # --- PC-Rahmen ---
        def clear(self, r, g, b):
            self._clear(r & 255, g & 255, b & 255)

        def finish(self):
            self._finish(1)  # vsync

        def width(self):
            return self._width()

        def height(self):
            return self._height()

        def poll(self):
            self._input_poll()

        # Button entweder als int (enum-Wert) oder als String ("IB_BACK") ...
        def _btn(self, button):
            if isinstance(button, str):
                try:
                    return self.BUTTONS[button]
                except KeyError:
                    raise KeyError("unbekannter Button: " + button)
            return int(button)

        def input_pressed(self, button, player=0):
            return bool(self._input_pressed(self._btn(button), int(player)))

        def input_released(self, button, player=0):
            return bool(self._input_released(self._btn(button), int(player)))

        def input_held(self, button, player=0):
            return bool(self._input_held(self._btn(button), int(player)))

        # Maus-Cursor in logischen GUI-Koordinaten (passt zu width()/height()
        # waehrend begin_gui und zu gfx.text/gfx.texquad). Rueckgabe: (x, y).
        def pointer(self):
            x = ctypes.c_int(0)
            y = ctypes.c_int(0)
            self._pointer_gui(ctypes.byref(x), ctypes.byref(y))
            return (x.value, y.value)

        # Setzt gstate.quit/reboot = true -> CavEX beendet die Hauptschleife und
        # startet danach neu (execv). Wirkt nach dem aktuellen Frame.
        def reboot(self):
            self._reboot()

        # --- Render ---
        def texture(self, enable):
            self._texture(1 if enable else 0)

        def bind_texture(self, tex_ref):
            if isinstance(tex_ref, str):
                tex_ref = self.tex[tex_ref]
            self._bind_texture(int(tex_ref))

        def text(self, x, y, s, scale, shadow, setup=True):
            if setup:
                self.begin_gui()
            if isinstance(s, str):
                s = s.encode("utf-8")
            self._text(int(x), int(y), s, int(scale), 1 if shadow else 0)

        def texquad(self, x, y, tx, ty, sx, sy, width, height, setup=True):
            if setup:
                self.begin_gui()
            self._texquad(int(x), int(y), int(tx), int(ty),
                          int(sx), int(sy), int(width), int(height))

        def license(self, width, height):
            self._license(int(width), int(height))

        def text_col(self, col):
            return self._text_col(int(col))

        def font_width(self, s, scale):
            if isinstance(s, str):
                s = s.encode("utf-8")
            return self._font_width(s, int(scale))

        def window(self, x, y, width, height, title, setup=True):
            if setup:
                self.begin_gui()
            if isinstance(title, str):
                title = title.encode("utf-8")
            self._window(int(x), int(y), int(width), int(height), title)

        def bg(self, setup=True):
            if setup:
                self.begin_gui()
            self._bg()

        def bg_panorama(self, setup=True):
            if setup:
                self.begin_gui()
            self._bg_panorama()

        def scissor(self, enable, x=0, y=0, width=0, height=0):
            self._scissor(1 if enable else 0, int(x), int(y),
                          int(width), int(height))

        def dim_box(self, x, y, width, height, setup=True):
            if setup:
                self.begin_gui()
            self._dim_box(int(x), int(y), int(width), int(height))

    gfx = CavexGfx({
        "bind_texture": gfx_bind_texture_ptr, "text": gutil_text_ptr,
        "texquad": gutil_texquad_ptr, "license": gutil_license_ptr,
        "text_col": gutil_text_col_ptr, "font_width": gutil_font_width_ptr,
        "mode_gui": gfx_mode_gui_ptr, "texture": gfx_texture_ptr,
        "window": gutil_window_ptr, "bg": gutil_bg_ptr,
        "bg_panorama": gutil_bg_panorama_ptr, "scissor": gfx_scissor_ptr,
        "dim_box": dim_box_ptr, "clear": gfx_clear_ptr, "finish": gfx_finish_ptr,
        "width": gfx_width_ptr, "height": gfx_height_ptr,
        "input_poll": input_poll_ptr, "input_pressed": input_pressed_ptr,
        "input_released": input_released_ptr, "input_held": input_held_ptr,
        "pointer_gui": pointer_gui_ptr, "reboot": reboot_ptr,
    }, tex_ptrs)

    # Versions-/Plattform-Infos (aus run_python.c) an gfx haengen -> in main.py
    # ueber gfx.version, gfx.platform, gfx.version_major usw. erreichbar.
    gfx.game_name = game_name
    gfx.version = version
    gfx.version_major = version_major
    gfx.version_minor = version_minor
    gfx.version_patch = version_patch
    gfx.version_fork = version_fork
    gfx.version_impl = version_impl
    gfx.platform = platform
    gfx.license = license
    gfx.copyright = copyright

    # enum input_button (siehe platform/input.h)
    IB_HOME = 9
    IB_BACK = 19

    txt_size = 4

    # HTTP-GET ueber urllib. Zertifikatspruefung aus (self-signed Server),
    # analog zu curl_get(verify_peer=0). Wirft bei Fehler -> Crash-Screen.
    def http_get(url):
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        req = urllib.request.Request(url, headers={"User-Agent": "ffCavEX-PC"})
        with urllib.request.urlopen(req, timeout=30, context=ctx) as r:
            return r.read()

    def loading_screen(txt):
        gfx.poll()
        gfx.clear(255, 255, 255)
        wdt, hgt = gfx.width(), gfx.height()
        gfx.text(int(wdt / 2 - gfx.font_width(txt, txt_size) / 2),
                 int(hgt / 2 - 10), txt, txt_size, True)
        gfx.finish()

    with open("Cavex/config_pc.json", "r") as f:
        config = json.load(f)
    os.makedirs(config["paths"]["tmp"], exist_ok=True)

    loading_screen("loading data info ...")
    body = http_get("https://192.168.15.188:5010/en/pc/get/data_info.json")
    data_info = json.loads(body)["data"]

    cancelled = False
    for i, data in enumerate(data_info):
        loading_screen("loading data ... (" + str(i) + "/" + str(len(data_info)) + ")")
        content = http_get(data[0])
        with open(config["paths"]["tmp"] + "/" + data[1], "wb") as f:
            f.write(content)
        if gfx.input_pressed(IB_BACK) or gfx.input_pressed(IB_HOME):
            cancelled = True
            break

    if not cancelled:
        loading_screen("running main.py ...")

        import sys

        root_of_file = os.getcwd()
        os.chdir(config["paths"]["tmp"])
        # main.py importiert Geschwistermodule (screen, plt, ...). cwd ist NICHT
        # in sys.path und run_path fuegt das Skriptverzeichnis nicht hinzu ->
        # tmp-Ordner explizit eintragen, sonst ModuleNotFoundError.
        sys.path.insert(0, os.getcwd())
        try:
            # startup_arg (z.B. "no_resources") kommt aus run_python.c und wird
            # an main.py weitergereicht, das darauf reagiert (Ressourcen laden).
            runpy.run_path("main.py",
                           init_globals={"gfx": gfx,
                                         "arg": globals().get("startup_arg", "")},
                           run_name="__main__")
        finally:
            os.chdir(root_of_file)

except Exception as e:
    # Auf PC ist die volle Stdlib nutzbar -> traceback ist ok (der sre-Bug ist
    # Wii-spezifisch). Traceback auf stderr (Terminal) UND, falls gfx bereit ist,
    # auf den Bildschirm.
    import traceback
    tb_text = "".join(traceback.format_exception(type(e), e, e.__traceback__))
    try:
        import sys
        sys.stderr.write(tb_text + "\n")
    except Exception:
        pass

    if gfx is not None:
        lines = []
        for raw in tb_text.replace("\t", "    ").split("\n"):
            while len(raw) > 90:
                lines.append(raw[:90])
                raw = raw[90:]
            lines.append(raw)

        line_h = 18
        top = 10
        max_lines = int((gfx.height() - 40 - top) / line_h)

        while True:
            gfx.poll()
            gfx.clear(255, 0, 0)
            y = top
            for ln in lines[:max_lines]:
                gfx.text(10, int(y), ln, 1, True)
                y += line_h
            gfx.text(10, int(gfx.height() - 30), "BACK/HOME = zurueck", 1, True)
            if gfx.input_pressed(IB_HOME) or gfx.input_pressed(IB_BACK):
                break
            gfx.finish()
