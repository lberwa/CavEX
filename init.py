
try:
  import wiitools as w
  import json
  import runpy
  import os

  w.rendering_adopt(mode_ptr, fb_ptr)
  d = True
  txt_size = 2

  # Wrapper um die CavEX-Renderfunktionen. Die Adressen kommen als Integer aus
  # run_python.c (init_globals) und werden hier ueber wiitools.c_run(addr, ...)
  # aufgerufen. c_run nutzt die PPC-EABI-Konvention (r3..r10, max. 8 Argumente,
  # int-Rueckgabe in r3); Strings werden als Bytes uebergeben (c_run reicht den
  # Pufferzeiger durch, CPython terminiert Bytes intern mit \0).
  class CavexGfx:
      def __init__(self, bind_texture, text, texquad, license, text_col, tex,
                   mode_gui, texture, window, bg, bg_panorama, scissor, dim_box,
                   font_width):
          self._bind_texture = bind_texture
          self._text = text
          self._texquad = texquad
          self._license = license
          self._text_col = text_col
          self._mode_gui = mode_gui
          self._texture = texture
          self._window = window
          self._bg = bg
          self._bg_panorama = bg_panorama
          self._scissor = scissor
          self._dim_box = dim_box
          self._font_width = font_width
          # dict {name: adresse} der CavEX-Textur-Globals (aus run_python.c)
          self.tex = tex

      # WICHTIG: gutil_* nutzen CavEX' GX-Pfad (gfx_draw_quads, GX_VTXFMT2 mit
      # GX_Color4u8/DIRECT). wiitools' draw_rect/render_text setzen aber einen
      # anderen Vertex-Deskriptor -> ruft man gutil_* danach ohne Neu-Setup auf,
      # liest der GP die falsche Byte-Zahl pro Vertex und STALLT (Haenger).
      # gfx_mode_gui() stellt den GUI-Zustand her (Ortho + CLR0=DIRECT +
      # Blending), gfx_texture(True) schaltet TEV auf MODULATE (Textur sichtbar).
      def begin_gui(self):
          # wiitools.gutil_prepare() setzt den KOMPLETTEN GX-Zustand fuer den
          # VTXFMT2-Pfad (POS/CLR0/TEX0=DIRECT, TEV MODULATE, TexGen, Ortho
          # -256..256). CavEX' gfx_mode_gui() reicht NICHT, weil es TEX0/TexGens
          # nicht anfasst -> der GP haengt sonst in GX_DrawDone.
          w.gutil_prepare()

      # Bildschirmgroesse (Framebuffer). Als Methoden, damit die API zu init_pc.py
      # passt -> in main.py plattformuebergreifend gfx.width()/gfx.height().
      def width(self):
          return w.width

      def height(self):
          return w.height

      # Setzt gstate.quit/reboot = true -> CavEX beendet die Hauptschleife und
      # startet danach neu (boot_dol). Kehrt nicht "sofort" neu, sondern nach
      # dem aktuellen Frame.
      def reboot(self):
          return w.c_run(self._reboot)

      # gfx_texture(bool enable): TEV MODULATE (True) bzw. PASSCLR (False)
      def texture(self, enable):
          return w.c_run(self._texture, 1 if enable else 0)

      # gfx_bind_texture(struct tex_gfx *tex)
      # tex_ref: entweder eine Adresse (int) oder ein Name aus self.tex
      # (z.B. "gui2", "terrain", "items")
      def bind_texture(self, tex_ref):
          if isinstance(tex_ref, str):
              tex_ref = self.tex[tex_ref]
          return w.c_run(self._bind_texture, int(tex_ref))

      # gutil_text(int x, int y, const char *str, int scale, bool shadow)
      # setup=True stellt vorher den GUI-GX-Zustand her (verhindert den Haenger)
      def text(self, x, y, s, scale, shadow, setup=True):
          if setup:
              self.begin_gui()
          if isinstance(s, str):
              s = s.encode("utf-8")
          return w.c_run(self._text, int(x), int(y), s, int(scale),
                         1 if shadow else 0)

      # gutil_texquad(int x, int y, int tx, int ty, int sx, int sy, int w, int h)
      # Textur vorher via bind_texture(...) binden! setup=True setzt den
      # GUI-GX-Zustand (Ortho + CLR0=DIRECT + TEV MODULATE).
      def texquad(self, x, y, tx, ty, sx, sy, width, height, setup=True):
          if setup:
              self.begin_gui()
          return w.c_run(self._texquad, int(x), int(y), int(tx), int(ty),
                         int(sx), int(sy), int(width), int(height))

      # gutil_license(int width, int height)
      def license(self, width, height):
          return w.c_run(self._license, int(width), int(height))

      # gutil_text_col(int col) -> int
      def text_col(self, col):
          return w.c_run(self._text_col, int(col))

      # gutil_font_width(const char *str, int scale) -> int (Textbreite in px)
      def font_width(self, s, scale):
          if isinstance(s, str):
              s = s.encode("utf-8")
          return w.c_run(self._font_width, s, int(scale))

      # gutil_window(int x, int y, int width, int height, char title[])
      def window(self, x, y, width, height, title, setup=True):
          if setup:
              self.begin_gui()
          if isinstance(title, str):
              title = title.encode("utf-8")
          return w.c_run(self._window, int(x), int(y), int(width), int(height),
                         title)

      # gutil_bg(void): Standard-GUI-Hintergrund
      def bg(self, setup=True):
          if setup:
              self.begin_gui()
          return w.c_run(self._bg)

      # gutil_bg_panorama(void): Panorama-Hintergrund
      def bg_panorama(self, setup=True):
          if setup:
              self.begin_gui()
          return w.c_run(self._bg_panorama)

      # gfx_scissor(bool enable, x, y, width, height): Zeichenbereich begrenzen.
      # Kein begin_gui noetig -- reine GX-Zustandsfunktion.
      def scissor(self, enable, x=0, y=0, width=0, height=0):
          return w.c_run(self._scissor, 1 if enable else 0, int(x), int(y),
                         int(width), int(height))

      # Halbtransparente dunkle Box (x, y, width, height) -- kapselt den
      # 12-Arg-Aufruf gutil_texquad_col, der nicht direkt via c_run geht.
      def dim_box(self, x, y, width, height, setup=True):
          if setup:
              self.begin_gui()
          return w.c_run(self._dim_box, int(x), int(y), int(width), int(height))

  gfx = CavexGfx(gfx_bind_texture_ptr, gutil_text_ptr, gutil_texquad_ptr,
                 gutil_license_ptr, gutil_text_col_ptr, tex_ptrs,
                 gfx_mode_gui_ptr, gfx_texture_ptr,
                 gutil_window_ptr, gutil_bg_ptr, gutil_bg_panorama_ptr,
                 gfx_scissor_ptr, dim_box_ptr, gutil_font_width_ptr)

  # Adresse der Reboot-Funktion (cavex_request_reboot) an gfx haengen -> von
  # gfx.reboot() genutzt.
  gfx._reboot = reboot_ptr

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

  with open("config_wii.json", "r") as f:
    config = json.load(f)
  
  os.makedirs(config["paths"]["tmp"], exist_ok=True)

  w.net_init() # IsNetReady() give false if not called before
  
  while not w.IsNetReady():
    w.draw_rect(0, 0, w.width, w.height, (255, 255, 255, 255), 0)
    w.render_text(int(w.width/2 - w.text_length("waiting for network ...", txt_size)), int(w.height/2 - 10),
                   "waiting for network ...", txt_size, True, (0, 0, 255, 255), 0)
    w.update()


  while d:
    w.draw_rect(0, 0, w.width, w.height, (255, 255, 255, 255), 0)
    txt = "loading data info ..."
    w.render_text(int(w.width/2 - w.text_length(txt, txt_size)), int(w.height/2 - 10), 
                  txt, txt_size, True, (0, 0, 255, 255), 0)
    w.update()

    resp = w.curl_get("https://192.168.15.188:5010/en/wii/get/data_info.json")
    if not resp["ok"]:
        raise RuntimeError("curl_get data_info failed: status=" + str(resp["status"])
                           + " error=" + str(resp["error"]) + " curl_code=" + str(resp["curl_code"]))
    data_info = json.loads(resp["body"])["data"]



    for i, data in enumerate(data_info):
        w.draw_rect(0, 0, w.width, w.height, (255, 255, 255, 255), 0)
        txt = "loading data ... (" + str(i) + "/" + str(len(data_info)) + ")"
        w.render_text(int(w.width/2 - w.text_length(txt, txt_size)), int(w.height/2 - 10), txt, txt_size, True, (0, 0, 255, 255), 0)
        w.update()

        file_resp = w.curl_get(data[0])
        if not file_resp["ok"]:
            raise RuntimeError("curl_get " + str(data[0]) + " failed: status="
                               + str(file_resp["status"]) + " error=" + str(file_resp["error"]))
        with open(config["paths"]["tmp"] + "/" + data[1], "wb") as f:
            f.write(file_resp["body"])

        if w.WPAD_ButtonsDown(w.WPAD_BUTTON_B, 0) or w.WPAD_ButtonsDown(w.WPAD_BUTTON_HOME, 0):
            d = False
            break

  
    
    if not d:
        break
    
    w.draw_rect(0, 0, w.width, w.height, (255, 255, 255, 255), 0)
    txt = "running main.py ..."
    w.render_text(int(w.width/2 - w.text_length(txt, txt_size)), int(w.height/2 - 10), 
                  txt, txt_size, True, (0, 0, 255, 255), 0)
    w.update()

    import sys

    root_of_file = os.getcwd()
    os.chdir(config["paths"]["tmp"])
    # main.py importiert Geschwistermodule (screen, plt, ...). Das eingebettete
    # CPython hat cwd NICHT in sys.path und run_path fuegt das Skriptverzeichnis
    # nicht hinzu -> tmp-Ordner explizit eintragen, sonst ModuleNotFoundError.
    sys.path.insert(0, os.getcwd())
    try:
        # startup_arg (z.B. "no_resources") kommt aus run_python.c und wird an
        # main.py weitergereicht, das darauf reagiert (Ressourcen nachladen).
        runpy.run_path("main.py",
                       init_globals={"gfx": gfx,
                                     "arg": globals().get("startup_arg", "")},
                       run_name="__main__")
    finally:
        os.chdir(root_of_file)

    d = False
except Exception as e:
    # HINWEIS: KEIN "import traceback" / "linecache" / "re" / "tokenize"!
    # Die abgespeckte Wii-Python-Stdlib hat eine kaputte sre-Kategorie-Tabelle
    # (KeyError: CATEGORY_NOT_WORD beim Regex-Kompilieren) -> jeder dieser
    # Imports wuerde hier selbst crashen und den echten Fehler verschlucken.
    # Deshalb bauen wir den Traceback von Hand.

    # kleiner Datei-Cache, damit dieselbe Quelldatei nicht mehrfach gelesen wird
    _src_cache = {}

    def _source_line(fname, lineno):
        # liest die Quellzeile direkt via open() (ohne linecache/tokenize)
        if not fname or lineno is None or lineno < 1:
            return None
        if fname not in _src_cache:
            try:
                with open(fname, "rb") as _f:
                    _src_cache[fname] = _f.read().decode("utf-8", "replace").split("\n")
            except Exception:
                _src_cache[fname] = None
        src = _src_cache[fname]
        if src and 1 <= lineno <= len(src):
            return src[lineno - 1]
        return None

    def _caret_cols(tb):
        # exakte Spalten des Fehler-Ausdrucks aus co_positions (Python 3.11+),
        # damit die ^^^^-Markierung sitzt; ohne Regex
        try:
            positions = list(tb.tb_frame.f_code.co_positions())
            idx = tb.tb_lasti // 2
            if 0 <= idx < len(positions):
                sl, el, sc, ec = positions[idx]
                if sl == el and sc is not None and ec is not None and ec > sc:
                    return sc, ec
        except Exception:
            pass
        return None, None

    # Traceback-Frames selbst durchlaufen und im Standard-Format aufbereiten
    raw_lines = ["Traceback (most recent call last):"]
    tb = e.__traceback__
    while tb is not None:
        code = tb.tb_frame.f_code
        fname = code.co_filename
        lineno = tb.tb_lineno
        raw_lines.append('  File "' + str(fname) + '", line ' + str(lineno)
                         + ", in " + str(code.co_name))
        src = _source_line(fname, lineno)
        if src is not None:
            stripped = src.lstrip()
            indent = len(src) - len(stripped)
            raw_lines.append("    " + stripped)
            sc, ec = _caret_cols(tb)
            if sc is not None:
                raw_lines.append("    " + " " * (sc - indent) + "^" * (ec - sc))
        tb = tb.tb_next
    raw_lines.append(type(e).__name__ + ": " + str(e))

    # in einzelne Zeilen aufteilen; Tabs zu Leerzeichen, damit die
    # ^^^^-Markierung unter der richtigen Spalte steht
    lines = []
    for raw in "\n".join(raw_lines).replace("\t", "    ").split("\n"):
        # zu lange Zeilen umbrechen, damit nichts aus dem Bild laeuft
        while len(raw) > 90:
            lines.append(raw[:90])
            raw = raw[90:]
        lines.append(raw)

    # sicherstellen, dass wir zeichnen koennen (falls der Crash vor
    # rendering_adopt passierte, ist es hier ein harmloses erneutes Setzen)
    try:
        w.rendering_adopt(mode_ptr, fb_ptr)
    except Exception:
        pass

    line_h = 18          # Zeilenhoehe in Pixeln
    top = 10             # Abstand von oben
    max_lines = int((w.height - 40 - top) / line_h)  # Platz bis "HOME = zurueck"

    while True:
        w.draw_rect(0, 0, w.width, w.height, (255, 0, 0, 255), 0)

        y = top
        for ln in lines[:max_lines]:
            w.render_text(10, int(y), ln, 1, True, (255, 255, 255, 255), 0)
            y += line_h

        w.render_text(10, int(w.height - 30), "HOME = zurueck",
                      1, True, (255, 255, 255, 255), 0)
        if w.WPAD_ButtonsDown(w.WPAD_BUTTON_HOME, 0):
            break
        w.update()