#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
.SECONDARY:
#---------------------------------------------------------------------------------
# prevent deletion of implicit targets
#---------------------------------------------------------------------------------
.SECONDARY:
#---------------------------------------------------------------------------------

ifeq ($(IS_PC_BUILD), 0)

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

# Welche libogc 3.1.0 wird benutzt?
#   OLD_BUILD=0 (Default): installierte libogc unter $(DEVKITPRO)/libogc
#   OLD_BUILD=1          : selbst gebaute libogc unter ../libogc (Entwicklung,
#                          wenn man libogc lokal patcht). Beide sind 3.1.0.
OLD_BUILD ?= 0

ifeq ($(OLD_BUILD), 1)
MYLIBOGC          := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../libogc
LIBOGC_INCFLAGS   := -I$(MYLIBOGC)/gc -I$(MYLIBOGC)/external/bsd/include
export LIBOGC_INC := $(MYLIBOGC)/gc
export LIBOGC_LIB := $(MYLIBOGC)/lib/wii
else
LIBOGC_INCFLAGS   := -I$(DEVKITPRO)/libogc/include
export LIBOGC_INC := $(DEVKITPRO)/libogc/include
export LIBOGC_LIB := $(DEVKITPRO)/libogc/lib/wii
endif
LIBOGC_LIBDIR     := $(LIBOGC_LIB)
endif
#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
MAKEFILE_DIR := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))

TARGET		:=	$(notdir $(CURDIR))
VERSION     :=  Alpha 0.3.0_f3
BUILD		:=	build
PC_BUILD	:=	buildpc
WIN_BUILD	:=	build_win
MAC_BUILD	:=	build_mac
CAVEX       :=  Cavex
INSTALL_USER := $(if $(SUDO_USER),$(SUDO_USER),$(USER))
INSTALL_HOME := $(if $(SUDO_USER),$(shell getent passwd "$(SUDO_USER)" | cut -d: -f6),$(HOME))
HOME_PATH   :=  $(INSTALL_HOME)/.cavex
INSTALL_ROOT := $(HOME_PATH)
CAVEX_DIR   :=  $(PC_BUILD)/$(CAVEX)
WIN_CAVEX_DIR := $(WIN_BUILD)/$(CAVEX)
WIN_CC      ?= x86_64-w64-mingw32-gcc
WIN_CXX     ?= x86_64-w64-mingw32-g++
WIN_CMAKE_TOOLCHAIN_FILE ?=
VCPKG_ROOT  ?= $(firstword $(wildcard $(HOME)/vcpkg $(HOME)/Downloads/vcpkg))
WIN_VCPKG_TRIPLET ?= x64-mingw-static
MAC_CC      ?= clang
MAC_CXX     ?= clang++
MAC_CMAKE_TOOLCHAIN_FILE ?=
MACOSX_SDK  ?=
MACOSX_ARCH ?= x86_64
MACOSX_DEPLOYMENT_TARGET ?= 10.13
ifeq ($(strip $(WIN_CMAKE_TOOLCHAIN_FILE)),)
ifneq ($(strip $(VCPKG_ROOT)),)
WIN_CMAKE_TOOLCHAIN_FILE := $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
endif
endif
nropt       ?=  $(shell nproc)
SOURCES		:=	source source/block source/entity source/graphics source/network \
				source/game source/game/gui source/platform source/item source/item/items \
				source/cNBT source/parson source/cubiomes source/boot \
				source/sound source/sound/mp3 source/sound/mp3/FileOperations \
				source/sound/mp3/Tools \
				source/recastnavigation/Recast/Source \
				source/recastnavigation/Detour/Source #source/lodepng
DATA		:=  
TEXTURES	:=	textures
INCLUDES	:=	source source/sound/mp3 \
				source/recastnavigation/Recast/Include \
				source/recastnavigation/Detour/Include

CPPFLAGS += -D__WII__ -DPLATFORM_WII
CFLAGS   += -D__WII__ -DPLATFORM_WII

CFLAGS += $(LIBOGC_INCFLAGS) -DBUILD_LIBOGC31

# SD-Trace-Log nach sd:/cavexlog.txt (Debug, synchron -> kann Races verstecken).
#CFLAGS += -DSD_LOG
#CPPFLAGS += -DSD_LOG

# CP_TRACE: RAM-Ringpuffer-Checkpoints, Thread flusht nach sd:/cptrace.txt.
# Zum Debuggen einkommentieren. Standardmaessig AUS.
#CFLAGS += -DCP_TRACE
#CPPFLAGS += -DCP_TRACE

# TEX_ATLAS_SDLOG: Diagnose fuer den Texture-Atlas-Crash nach Reboot. Schreibt
# je Atlas-Entry Puffer-Pointer, Groessen und die berechneten Max-Offsets nach
# sd:/texatlaslog.txt. Die letzte Zeile vor dem Crash zeigt die schuldige Entry
# (Out-of-Bounds an Quelle oder Ziel wird direkt sichtbar). Standardmaessig AUS.
#CFLAGS += -DTEX_ATLAS_SDLOG
#CPPFLAGS += -DTEX_ATLAS_SDLOG

# Netzwerk-Remote-Debug (debug_init/debug_send, TCP Port 12344).
# AUS: die vielen debug_send-Aufrufe im Code (z.B. in sound_play) feuern sonst
# net_send/IPC ab und crashen im gemeinsamen IPC-lwp_heap (mit asnd). Nur zum
# gezielten Debuggen einschalten -- und dann NICHT waehrend Sound/Gameplay.
#CFLAGS += -DNET_DEBUG
#CPPFLAGS += -DNET_DEBUG

# --- CPython in CavEX einlinken (ein Binary statt Overlay) ---------------
# -flto raus (Verdacht fuer Startprobleme mit prebuilt libpython), -D_POSIX_THREADS
# noetig, damit Python.h unter -std=c99 die pthread-Typen kennt.
CPYTHON_DIR ?= $(DEVKITPRO)/extras/cpython/3.15.0a7
PY_INCLUDE  := -I$(CPYTHON_DIR)/build-wii -I$(CPYTHON_DIR)/Include
PY_LIBDIRS  := -L$(LIBOGC_LIBDIR) \
               -L$(CPYTHON_DIR)/libs -L$(CPYTHON_DIR)/gdbm/install-wii/lib \
               -L$(CPYTHON_DIR)/xz/install-wii/lib \
               -L$(CPYTHON_DIR)/uuid/install-wii/lib \
               -L$(DEVKITPRO)/libogc/lib/wii \
               -L$(DEVKITPRO)/portlibs/ppc/lib

# libogc1-Variante (PPCDCacheFlushAsync/tuxedo) -- passt zu libogc 3.1.0 unter
# ../libogc: NICHT-suffigierte Libs (python, curl, fatpy, bitmap, wiidl) plus
# -lwiidl (wii_dlopen/dlsym/dlerror/dl_load_symbol_map) und -lwiikeyboard
# (KEYBOARD_Init/GetEvent/Deinit fuer das wiitools-Modul; aus ../libogc).
PY_LIBS     := -lpython3.15 -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -ltfpsacrypto \
               -lz -lgdbm_compat -lgdbm -llzma -lbz2 -luuid -lfatpy -lbitmap \
               -lwiidl -lwiikeyboard

CXXFLAGS	+=	$(CFLAGS)
CPPFLAGS	+=	-Ofast -DSPLITSCREEN=2 -g
CFLAGS		+=	-Ofast -g -std=c99 -pedantic -Wextra -Wno-unused-parameter -Wall \
				-DSPLITSCREEN=2  -DPLATFORM_WII -D_POSIX_THREADS \
				$(PY_INCLUDE) $(MACHDEP) $(INCLUDE) -DWITH_PYTHON -DUSLEEP \
				-DNET_DEBUG

LDFLAGS	+=	$(MACHDEP) -Wl,-Map,$(notdir $@).map
LDFLAGS += -L$(MAKEFILE_DIR)
LDFLAGS += -Wl,--allow-multiple-definition

# libogc 3.1.0: $(LIBOGC_LIBDIR) ist je nach OLD_BUILD ../libogc/lib/wii oder
# $(DEVKITPRO)/libogc/lib/wii. Standard-Boot (crtmain.o / default rvl.ld), KEINE
# 2.12.0-Altlasten (ogc_crt0.o/system.o/system_asm.o) und KEIN eigenes
# Linkerskript -- 3.1.0 braucht __gxregs/__stack_*/__intrstack_* nicht.
# $(DEVKITPRO)/libogc/lib/wii zusaetzlich als Fallback (u.a. -lfat).
LDFLAGS += -L$(LIBOGC_LIBDIR)
LDFLAGS += -L$(DEVKITPRO)/libogc/lib/wii
# --start-group fuer die wechselseitigen Abhaengigkeiten der Python-/libogc-Libs.
LIBS    :=  -Wl,--start-group $(PY_LIBS) -lwiiuse -lbte -lmad -lasnd -lfat -logc -Wl,--end-group -lm

#CAVEXFAT_LIB := $(abspath $(MAKEFILE_DIR)/libcavexfat.a)

LIBDIRS	:= $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))


export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
					$(foreach dir,$(TEXTURES),$(CURDIR)/$(dir))


export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
SCFFILES	:=	$(foreach dir,$(TEXTURES),$(notdir $(wildcard $(dir)/*.scf)))
TPLFILES	:=	$(SCFFILES:.scf=.tpl)

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(addsuffix .o,$(TPLFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o)


#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)
# Zusätzliche Include-Pfade für parson und lodepng
export INCLUDE += -I/opt/devkitpro/portlibs/wii/include

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					-L$(LIBOGC_LIB)
export LIBPATHS += -L/opt/devkitpro/portlibs/wii/lib
export LIBPATHS += $(PY_LIBDIRS)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean
.PHONY: wii pc pc-clean win win-clean win-run mac mac-clean mac-run uninstall

#---------------------------------------------------------------------------------
pc:
	@[ -d $(PC_BUILD) ] || mkdir -p $(PC_BUILD)
	@[ -d $(PC_BUILD)/tmp ] || mkdir -p $(PC_BUILD)/tmp
	@rm -rf $(PC_BUILD)/saves $(PC_BUILD)/assets
	@rm -f $(PC_BUILD)/source
	@mkdir -p $(CAVEX_DIR)
	@cp $(CURDIR)/2CMakeLists.txt $(PC_BUILD)/CMakeLists.txt
	@ln -s $(CURDIR)/source $(PC_BUILD)/
	@cp -r $(CURDIR)/assets $(CAVEX_DIR)/
	@cp -r $(CURDIR)/saves $(CAVEX_DIR)/
	@cp $(CURDIR)/config_pc.json $(CAVEX_DIR)/
	@cp $(CURDIR)/init_pc.py $(CAVEX_DIR)/init.py
	@cd $(PC_BUILD) && unset CC CXX CPPFLAGS CFLAGS CXXFLAGS LDFLAGS && TMPDIR="$(CURDIR)/$(PC_BUILD)/tmp" cmake -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF .
	@cd $(PC_BUILD) && env TMPDIR="$(CURDIR)/$(PC_BUILD)/tmp" $(MAKE) -j$(nropt)

#---------------------------------------------------------------------------------
# Windows-Cross-Build (MinGW). Optional vcpkg/toolchain file can be supplied with:
#   make win IS_PC_BUILD=1 WIN_CMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
win:
	@test "$(IS_PC_BUILD)" = "1" || { echo "Bitte mit IS_PC_BUILD=1 aufrufen"; exit 1; }
	@command -v "$(WIN_CC)" >/dev/null 2>&1 || { echo "Fehlt: $(WIN_CC) (MinGW installieren)"; exit 1; }
	@command -v "$(WIN_CXX)" >/dev/null 2>&1 || { echo "Fehlt: $(WIN_CXX) (MinGW installieren)"; exit 1; }
	@[ -d $(WIN_BUILD) ] || mkdir -p $(WIN_BUILD)
	@[ -d $(WIN_BUILD)/tmp ] || mkdir -p $(WIN_BUILD)/tmp
	@rm -rf $(WIN_BUILD)/saves $(WIN_BUILD)/assets
	@rm -f $(WIN_BUILD)/source
	@mkdir -p $(WIN_CAVEX_DIR)
	@cp $(CURDIR)/2CMakeLists.txt $(WIN_BUILD)/CMakeLists.txt
	@ln -s $(CURDIR)/source $(WIN_BUILD)/
	@cp -r $(CURDIR)/assets $(WIN_CAVEX_DIR)/
	@cp -r $(CURDIR)/saves $(WIN_CAVEX_DIR)/
	@cp $(CURDIR)/config_pc.json $(WIN_CAVEX_DIR)/
	@cp $(CURDIR)/init_pc.py $(WIN_CAVEX_DIR)/init.py
	@cd $(WIN_BUILD) && unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS && env CC="$(WIN_CC)" CXX="$(WIN_CXX)" TMPDIR="$(CURDIR)/$(WIN_BUILD)/tmp" cmake -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER="$(WIN_CC)" -DCMAKE_CXX_COMPILER="$(WIN_CXX)" $(if $(WIN_CMAKE_TOOLCHAIN_FILE),-DCMAKE_TOOLCHAIN_FILE="$(WIN_CMAKE_TOOLCHAIN_FILE)" -DVCPKG_TARGET_TRIPLET="$(WIN_VCPKG_TRIPLET)",) -DIS_PC_BUILD=1 -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF .
	@cd $(WIN_BUILD) && env TMPDIR="$(CURDIR)/$(WIN_BUILD)/tmp" $(MAKE) -j$(nropt)
	@winpthread_dll="$$($(WIN_CC) -print-file-name=libwinpthread-1.dll)"; \
	test -f "$$winpthread_dll" || { echo "Fehlt: libwinpthread-1.dll"; exit 1; }; \
	cp -f "$$winpthread_dll" $(WIN_BUILD)/

win-run: win
	@command -v wine >/dev/null 2>&1 || { echo "Fehlt: wine"; exit 1; }
	@cd $(WIN_BUILD) && wine ./cavex.exe

win-clean:
	@echo "cleaning ..."
	@rm -rf $(WIN_BUILD)

#---------------------------------------------------------------------------------
# macOS-Build. Auf macOS wird der installierte Apple-Clang und das Apple-SDK
# verwendet. Von Linux aus ist ein macOS-SDK plus ein Darwin-kompatibler Clang
# erforderlich; ohne SDK wird bewusst mit einer verständlichen Meldung abgebrochen.
mac:
	@test "$(IS_PC_BUILD)" = "1" || { echo "Bitte mit IS_PC_BUILD=1 aufrufen"; exit 1; }
	@command -v "$(MAC_CC)" >/dev/null 2>&1 || { echo "Fehlt: $(MAC_CC) (Clang installieren)"; exit 1; }
	@command -v "$(MAC_CXX)" >/dev/null 2>&1 || { echo "Fehlt: $(MAC_CXX) (Clang installieren)"; exit 1; }
	@[ -d $(MAC_BUILD) ] || mkdir -p $(MAC_BUILD)
	@[ -d $(MAC_BUILD)/tmp ] || mkdir -p $(MAC_BUILD)/tmp
	@rm -rf $(MAC_BUILD)/saves $(MAC_BUILD)/assets
	@rm -f $(MAC_BUILD)/source
	@mkdir -p $(MAC_BUILD)/$(CAVEX)
	@cp $(CURDIR)/2CMakeLists.txt $(MAC_BUILD)/CMakeLists.txt
	@ln -s $(CURDIR)/source $(MAC_BUILD)/
	@cp -r $(CURDIR)/assets $(MAC_BUILD)/$(CAVEX)/
	@cp -r $(CURDIR)/saves $(MAC_BUILD)/$(CAVEX)/
	@cp $(CURDIR)/config_pc.json $(MAC_BUILD)/$(CAVEX)/
	@cp $(CURDIR)/init_pc.py $(MAC_BUILD)/$(CAVEX)/init.py
	@cd $(MAC_BUILD) && unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS && \
	if [ "$$(uname -s)" = "Darwin" ]; then \
		env CC="$(MAC_CC)" CXX="$(MAC_CXX)" MACOSX_DEPLOYMENT_TARGET="$(MACOSX_DEPLOYMENT_TARGET)" \
		cmake -DIS_PC_BUILD=1 -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF $(if $(MAC_CMAKE_TOOLCHAIN_FILE),-DCMAKE_TOOLCHAIN_FILE="$(MAC_CMAKE_TOOLCHAIN_FILE)",); \
	else \
		test -n "$(MACOSX_SDK)" || { echo "Linux-Cross-Build benötigt MACOSX_SDK=/pfad/zum/macOS-SDK"; exit 1; }; \
		env CC="$(MAC_CC)" CXX="$(MAC_CXX)" MACOSX_DEPLOYMENT_TARGET="$(MACOSX_DEPLOYMENT_TARGET)" \
		cmake -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_C_COMPILER="$(MAC_CC)" -DCMAKE_CXX_COMPILER="$(MAC_CXX)" \
		-DCMAKE_OSX_SYSROOT="$(MACOSX_SDK)" -DCMAKE_OSX_ARCHITECTURES="$(MACOSX_ARCH)" \
		-DCMAKE_OSX_DEPLOYMENT_TARGET="$(MACOSX_DEPLOYMENT_TARGET)" -DIS_PC_BUILD=1 -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
		$(if $(MAC_CMAKE_TOOLCHAIN_FILE),-DCMAKE_TOOLCHAIN_FILE="$(MAC_CMAKE_TOOLCHAIN_FILE)",); \
	fi
	@cd $(MAC_BUILD) && env TMPDIR="$(CURDIR)/$(MAC_BUILD)/tmp" $(MAKE) -j$(nropt)

mac-run: mac
	@cd $(MAC_BUILD) && ./cavex

mac-clean:
	@echo "cleaning ..."
	@rm -rf $(MAC_BUILD)

pc-just-make:
	@[ -d $(PC_BUILD) ] || mkdir -p $(PC_BUILD)
	@[ -d $(PC_BUILD)/tmp ] || mkdir -p $(PC_BUILD)/tmp

	@rm -f $(PC_BUILD)/source
	@ln -s $(CURDIR)/source $(PC_BUILD)/

	@cp $(CURDIR)/2CMakeLists.txt $(PC_BUILD)/CMakeLists.txt

	@cd $(PC_BUILD) && unset CC CXX CPPFLAGS CFLAGS CXXFLAGS LDFLAGS && TMPDIR="$(CURDIR)/$(PC_BUILD)/tmp" cmake -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF .
	@cd $(PC_BUILD) && env TMPDIR="$(CURDIR)/$(PC_BUILD)/tmp" $(MAKE) -j$(nropt)

config:
	@jq --arg home "$(HOME_PATH)" \
	'.paths = {texturepack: ($$home+"/assets"), worlds: ($$home+"/saves"), bg: ($$home+"/bg"), MP3: ($$home+"/assets/sound"), sounds: ($$home+"/assets/sound/sound"), tmp: ($$home+"/tmp")}' \
	install_config_pc.json > $(PC_BUILD)/install_config_pc.json


install_desktop:
	@mkdir -p "$(INSTALL_HOME)/.local/share/applications"
	@printf '%s\n' \
		'[Desktop Entry]' \
		'Name=Cavex' \
		'Version=$(VERSION)' \
		'Comment=Your favourite block game' \
		'Exec=/usr/local/bin/cavex' \
		'Icon=$(HOME_PATH)/icon.png' \
		'Terminal=false' \
		'Type=Application' \
		'Categories=Game;' \
		'StartupNotify=true' \
		> "$(INSTALL_HOME)/.local/share/applications/cavex.desktop"


pc-install: pc install_desktop
	@if [ "$$(id -u)" != "0" ]; then \
		echo ""; \
		echo "please try "sudo make pc-install""; \
		echo ""; \
		exit 1; \
	fi

	@mkdir -p "$(HOME_PATH)"
	@mkdir -p "$(INSTALL_ROOT)"
	@cp -r $(CAVEX_DIR)/assets "$(HOME_PATH)/"
	@cp -r $(CAVEX_DIR)/saves "$(HOME_PATH)/"
	@cp     $(CAVEX_DIR)/init.py "$(HOME_PATH)/init.py"
	@cp     $(PC_BUILD)/cavex "$(INSTALL_ROOT)/cavex"
	@chmod +x "$(INSTALL_ROOT)/cavex"
	@ln -sfn "$(INSTALL_ROOT)/cavex" /usr/local/bin/cavex
	@cp     $(MAKEFILE_DIR)/pc-icon.png $(HOME_PATH)/icon.png
	@chown -R "$(INSTALL_USER)" "$(HOME_PATH)"

	@echo "installation complete!"


uninstall:
	@if [ "$$(id -u)" != "0" ]; then \
		echo ""; \
		echo "please try \"sudo make uninstall\""; \
		echo ""; \
		exit 1; \
	fi
	@rm -f /usr/local/bin/cavex
	@rm -f "$(INSTALL_ROOT)/cavex"
	@rmdir "$(INSTALL_ROOT)" 2>/dev/null || true
	@rm -f "$(INSTALL_HOME)/.local/share/applications/cavex.desktop"
	@echo "uninstallation complete; user data in $(HOME_PATH) was kept."


pc-clean:
	@echo "cleaning ..."
	@rm -rf $(PC_BUILD)
	
wii: $(BUILD) #server-client


#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	cp build/boot.dol boot.dol
	cp build/boot.elf boot.elf
#	cp boot.dol ready_wii/
#	cp boot.elf ready_wii/

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol
#---------------------------------------------------------------------------------
run:
	wiiload $(output).dol

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: boot.dol boot.elf

boot.dol: $(OUTPUT).dol
	cp $< $@
boot.elf: $(OUTPUT).elf
	cp $< $@


$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
	$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@


#---------------------------------------------------------------------------------
# This rule links in binary data with the .bin extension
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.tpl.o	:	%.tpl
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)


-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# server-client: baut das CPython-Overlay-Modul server_client/client.c zu
# server_client/ext.dol. Overlay-Link (kein crt0) bei 0x91000000, Entry
# _ext_start (siehe server_client/extension.ld). Unbedingtes Ziel -- laeuft
# unabhaengig von IS_PC_BUILD:  make server-client
#---------------------------------------------------------------------------------
#CPYTHON_DIR ?= $(DEVKITPRO)/extras/cpython/3.15.0a7
#SC_DIR      := server_client
#SC_CC       := $(DEVKITPPC)/bin/powerpc-eabi-gcc
#SC_ELF2DOL  := $(DEVKITPPC)/bin/elf2dol
#SC_INCLUDE  := -I$(CPYTHON_DIR)/build-wii -I$(CPYTHON_DIR)/Include \
               -I$(DEVKITPRO)/libogc/include
#SC_CFLAGS   := -Os -std=gnu99 -Wall -mrvl -fdata-sections -ffunction-sections \
               $(SC_INCLUDE)
#SC_LDFLAGS  := -nostartfiles -Wl,--gc-sections -T$(SC_DIR)/extension.ld \
               -L$(CPYTHON_DIR)/libs -L$(DEVKITPRO)/libogc/lib/wii \
               -L$(DEVKITPRO)/portlibs/ppc/lib \
               -L$(CPYTHON_DIR)/gdbm/install-wii/lib \
               -L$(CPYTHON_DIR)/xz/install-wii/lib \
               -L$(CPYTHON_DIR)/uuid/install-wii/lib
#SC_LIBS     := -lpython3.15 -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
               -ltfpsacrypto -lz -lgdbm_compat -lgdbm -llzma -lbz2 -luuid \
               -lfatpy -lwiiuse -lbte -lbitmap -logc -lm

#.PHONY: server-client
#server-client:
#	@test -n "$(DEVKITPPC)" || { echo "DEVKITPPC nicht gesetzt"; exit 1; }
#	@test -f "$(CPYTHON_DIR)/libs/libpython3.15.a" || { \
		echo "libpython3.15.a fehlt in $(CPYTHON_DIR)/libs"; \
		echo "-> CPYTHON_DIR anpassen oder CPython fuer powerpc-eabi bauen"; \
		exit 1; }
#	$(SC_CC) $(SC_CFLAGS) $(SC_DIR)/client.c $(SC_DIR)/start.S \
		$(SC_LDFLAGS) $(SC_LIBS) -o $(SC_DIR)/ext.elf
#	$(SC_ELF2DOL) $(SC_DIR)/ext.elf $(SC_DIR)/ext.dol
#	@echo "server-client: $(SC_DIR)/ext.dol gebaut"
#---------------------------------------------------------------------------------
