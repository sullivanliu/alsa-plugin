asound_module_pcm_adi_dsp_LTLIBRARIES = libasound_module_pcm_adi_dsp.la

asound_module_pcm_adi_dspdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_adi_dsp_la_SOURCES = pcm_adi_dsp.c
libasound_module_pcm_adi_dsp_la_LIBADD = @ALSA_LIBS@
