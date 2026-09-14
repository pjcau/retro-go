set(COMPONENT_SRCDIRS "common emucore gui")
set(COMPONENT_ADD_INCLUDEDIRS "common emucore gui")
set(COMPONENT_REQUIRES "retro-go")
register_component()
# Atari 2600: Stella (OtherCrashOverride's stella-odroid-go cut of Stella 4.x)
rg_setup_compile_options(-O2 -DSOUND_SUPPORT=1 -DJUMP_TABLES_IN_TEXT_SECTION=1 -fexceptions -fno-rtti
    -Wno-unused-function -Wno-unused-variable -Wno-error=deprecated-declarations -Wno-error=reorder -Wno-format
    -Wno-error=class-memaccess -Wno-error=maybe-uninitialized -Wno-error=implicit-fallthrough -Wno-error=sign-compare -Wno-error=unused-but-set-variable)
