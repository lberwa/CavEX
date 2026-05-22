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
				source/cNBT source/parson #source/lodepng
DATA		:=
TEXTURES	:=	textures
INCLUDES	:=

CPPFLAGS += -D__WII__
CFLAGS += -D__WII__ -DPLATFORM_WII
CFLAGS += -D__WII__ -DPLATFORM_WII
CFLAGS += -DPLATFORM_WII
CPPFLAGS += -DPLATFORM_WII

CXXFLAGS	+=	$(CFLAGS)
CPPFLAGS	+=	-Ofast -DSPLITSCREEN=2 -g
CFLAGS		+=	-Ofast -g -std=c99 -pedantic -Wextra -Wno-unused-parameter -flto=auto -Wall \
				-DSPLITSCREEN=2 -DNDEBUG -DPLATFORM_WII \
				$(MACHDEP) $(INCLUDE)

LDFLAGS	+=	$(MACHDEP) -Wl,-Map,$(notdir $@).map
LDFLAGS += -L$(MAKEFILE_DIR)

CAVEXFAT_LIB := $(abspath $(MAKEFILE_DIR)/libcavexfat.a)

LIBS	:=	-logc -lwiiuse $(CAVEXFAT_LIB) -lbte -lm -lz -lmad -lasnd

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
		'Terminal=false' \
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
	@cp     $(MAKEFILE_DIR)/pc-icon.png $(HOME_PATH)/icon.png
	@chown -R "$(INSTALL_USER)" "$(HOME_PATH)"

	@echo "installation complete!"


pc-clean:
	@echo "cleaning ..."
	@rm -rf $(PC_BUILD)
	
wii: $(BUILD)


#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	cp build/boot.dol boot.dol
	cp build/boot.elf boot.elf
	cp boot.dol ready_wii/
	cp boot.elf ready_wii/

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
