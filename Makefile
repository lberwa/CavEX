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

# MY=1 → libogc-eigenes (stabil, original, funktioniert)
# MY=0 → libogc 3.1.0  (experimentell, Startup-Patch)
MY ?= 0

ifeq ($(MY), 1)
export LIBOGC_INC := $(DEVKITPRO)/libogc-eigenes/gc
export LIBOGC_LIB := $(DEVKITPRO)/libogc-eigenes/lib/wii
endif
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
CAVEX       :=  Cavex
INSTALL_USER := $(if $(SUDO_USER),$(SUDO_USER),$(USER))
INSTALL_HOME := $(if $(SUDO_USER),$(shell getent passwd "$(SUDO_USER)" | cut -d: -f6),$(HOME))
HOME_PATH   :=  $(INSTALL_HOME)/.cavex
CAVEX_DIR   :=  $(PC_BUILD)/$(CAVEX)
nropt       ?=  $(shell nproc)
SOURCES		:=	source source/block source/entity source/graphics source/network \
				source/game source/game/gui source/platform source/item source/item/items \
				source/cNBT source/parson source/cubiomes source/boot #source/lodepng
DATA		:=  
TEXTURES	:=	textures
INCLUDES	:=

CPPFLAGS += -D__WII__ -DPLATFORM_WII
CFLAGS   += -D__WII__ -DPLATFORM_WII

ifeq ($(MY), 1)
CFLAGS += -I$(DEVKITPRO)/libogc-eigenes/gc
else
CFLAGS += -I$(DEVKITPRO)/libogc-eigenes/gc -DBUILD_LIBOGC31
endif

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
ifeq ($(MY), 1)
PY_LIBDIRS  := -L$(DEVKITPRO)/libogc-eigenes/lib/wii \
               -L$(CPYTHON_DIR)/libs -L$(CPYTHON_DIR)/gdbm/install-wii/lib \
               -L$(CPYTHON_DIR)/xz/install-wii/lib \
               -L$(CPYTHON_DIR)/uuid/install-wii/lib \
               -L$(DEVKITPRO)/portlibs/ppc/lib
else
PY_LIBDIRS  := -L$(DEVKITPRO)/libogc/lib/wii \
               -L$(CPYTHON_DIR)/libs -L$(CPYTHON_DIR)/gdbm/install-wii/lib \
               -L$(CPYTHON_DIR)/xz/install-wii/lib \
               -L$(CPYTHON_DIR)/uuid/install-wii/lib \
               -L$(DEVKITPRO)/portlibs/ppc/lib
endif

PY_LIBS     := -lpython3.15 -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -ltfpsacrypto \
               -lz -lgdbm_compat -lgdbm -llzma -lbz2 -luuid -lfatpy -lbitmap

CXXFLAGS	+=	$(CFLAGS)
CPPFLAGS	+=	-Ofast -DSPLITSCREEN=2 -g
CFLAGS		+=	-Ofast -g -std=c99 -pedantic -Wextra -Wno-unused-parameter -Wall \
				-DSPLITSCREEN=2  -DPLATFORM_WII -D_POSIX_THREADS \
				$(PY_INCLUDE) $(MACHDEP) $(INCLUDE) -DWITH_PYTHON -DUSLEEP

LDFLAGS	+=	$(MACHDEP) -Wl,-Map,$(notdir $@).map
LDFLAGS += -L$(MAKEFILE_DIR)
LDFLAGS += -Wl,--allow-multiple-definition

ifeq ($(MY), 1)
# MY=1: libogc-eigenes — original, kein Patch nötig
LDFLAGS += -L$(DEVKITPRO)/libogc-eigenes/lib/wii
LIBS    :=  $(PY_LIBS) -lwiiuse -lbte -lmad -lasnd -lfat -logc -lm
else
# MY=0: Startup + Basis-Libs aus lib/wii/ (HBC-kompatibel, im Repo enthalten)
# Nutzer braucht nur libogc 3.1.0 — libogc-eigenes ist nicht mehr nötig.
LDFLAGS += -L$(MAKEFILE_DIR)lib/wii
LDFLAGS += -L$(DEVKITPRO)/libogc/lib/wii
LDFLAGS += $(MAKEFILE_DIR)source/boot/ogc_crt0.o \
           $(MAKEFILE_DIR)source/boot/system_asm.o \
           $(MAKEFILE_DIR)source/boot/system.o
LIBS    :=  $(PY_LIBS) -lwiiuse -lbte -lmad -lasnd -lfat \
            $(MAKEFILE_DIR)lib/wii/libogc.a -logc -lm
endif

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
.PHONY: wii pc pc-clean

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
	'.paths = {texturepack: ($$home+"/assets"), worlds: ($$home+"/saves"), bg: ($$home+"/bg"), MP3: ($$home+"/mp32"), sounds: ($$home+"/mp32/sound"), tmp: ($$home+"/tmp")}' \
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
		'Terminal=false' \t

		'Type=Application' \
		'Categories=Game;' \
		'StartupNotify=true' \
		> "$(INSTALL_HOME)/.local/share/applications/cavex.desktop"


pc-install: pc config install_desktop
	@if [ "$$(id -u)" != "0" ]; then \
		echo ""; \
		echo "please try "sudo make pc-install""; \
		echo ""; \
		exit 1; \
	fi

	@cp -r $(CAVEX_DIR) /usr/local/bin
	@cp $(PC_BUILD)/cavex /usr/local/bin
	@chmod +x /usr/local/bin/cavex
	@mkdir -p "$(HOME_PATH)"
	@if [ ! -d "$(HOME_PATH)" ]; then \
		mv /usr/local/bin/$(CAVEX)/saves $(HOME_PATH)/; \
	fi
	@rm -rf $(HOME_PATH)/assets
	@rm -rf /usr/local/bin/$(CAVEX)/saves
	@mv     /usr/local/bin/$(CAVEX)/assets $(HOME_PATH)/
	@cp     $(PC_BUILD)/install_config_pc.json /usr/local/bin/$(CAVEX)/config_pc.json
	@cp     $(PC_BUILD)/init_pc.py /usr/local/bin/$(CAVEX)/init.py
	@cp     $(MAKEFILE_DIR)/pc-icon.png $(HOME_PATH)/icon.png
	@chown -R "$(INSTALL_USER)" "$(HOME_PATH)"

	@echo "installation complete!"


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
