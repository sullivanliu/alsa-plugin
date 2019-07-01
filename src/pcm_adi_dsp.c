/*
 * ALSA <-> ADI DSP extplugin
 *
 * Copyright 2014-2018 Analog Devices Inc.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <linux/soundcard.h>

#define CMD_VIRT_TO_PHYS		_IO('m',0)

struct mm_packet {
	unsigned long vaddr;
	unsigned long paddr;
};

/* ADSP parameters */

struct adsp_parms {
	int core_id;
	int mode;
	int frames;
};

typedef struct {
	/* instance and intermedate buffer */
	int state;
	int echoState;
	short *inputBuffer;	/* dsp input buffer */
	struct mm_packet inPkt;
	short *outputBuffer;	/* dsp output buffer */
	struct mm_packet outPkt;
} adsp_protocol;

typedef struct {
	snd_pcm_extplug_t ext; 
	struct adsp_parms parms;
	adsp_protocol protocol[2];
	char *device;
	int fd;
	/* running states */
	unsigned int filled;
	unsigned int processed;
} snd_pcm_adsp_t;

static inline void *area_addr(const snd_pcm_channel_area_t *area,
			      snd_pcm_uframes_t offset)
{
	unsigned int bitofs = area->first + area->step * offset;
	return (char *) area->addr + bitofs / 8;
}

static snd_pcm_sframes_t
adsp_transfer(snd_pcm_extplug_t *ext,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_adsp_t *adsp = ext->private_data;
	short *src = area_addr(src_areas, src_offset);
	short *dst = area_addr(dst_areas, dst_offset);
	unsigned int count = size;
	short *databuf;

	printf("FUNC %s IN\n", __func__);

#if 0
//	if (!spx->state && !spx->echo_state) {
		/* no DSP processing */
		memcpy(dst, src, count * 2);
		return size;
//	}
#else
#if 0
	if (adsp->echo_state)
		databuf = spx->outbuf;
	else
		databuf = spx->buf;
#endif
	databuf = adsp->protocol[0].inputBuffer;

	while (count > 0) {
		unsigned int chunk;
		if (adsp->filled + count > adsp->parms.frames)
			chunk = adsp->parms.frames - adsp->filled;
		else
			chunk = count;
		if (adsp->processed)
			memcpy(dst, databuf + adsp->filled, chunk * 2);
		else
			memset(dst, 0, chunk * 2);
		dst += chunk;
		memcpy(adsp->protocol[0].inputBuffer + adsp->filled, src, chunk * 2);
		adsp->filled += chunk;
		if (adsp->filled == adsp->parms.frames) {
			memcpy(adsp->protocol[0].outputBuffer,
						adsp->protocol[0].inputBuffer, adsp->parms.frames * 2);
			/* TODO: add the notify funtion for the DSP to process the data */
#if 0
			if (adsp->echo_state)
				speex_echo_capture(spx->echo_state, spx->buf,
						   spx->outbuf);
			if (spx->state)
				speex_preprocess_run(spx->state, databuf);
			if (spx->echo_state)
				speex_echo_playback(spx->echo_state, databuf);
#endif
			adsp->processed = 1;
			adsp->filled = 0;
		}
		src += chunk;
		count -= chunk;
	}
#endif
	
	printf("FUNC %s OUT\n", __func__);

	return size;
}

static int adsp_init(snd_pcm_extplug_t *ext)
{
	snd_pcm_adsp_t *adsp = ext->private_data;
	int i, ret;

	printf("FUNC %s IN\n", __func__);

	adsp->filled = 0;
	adsp->processed = 0;
	
	adsp->fd = open(adsp->device,O_RDWR);
	if (!adsp) {
		printf("Failed to open %s\n",adsp->device);
		exit(-1);
	}

	/* malloc the memeory for dsp input and output buffer */
	for (i = 0; i < 2; i++) {
		if (!adsp->protocol[i].inputBuffer) {
			adsp->protocol[i].inputBuffer= mmap(0,adsp->parms.frames * 2,
						PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, adsp->fd, 0);
			if (!adsp->protocol[i].inputBuffer) {
				printf("%s, core[%d] mmap failed\n", __LINE__, i);
				ret = -ENOMEM;
				goto error;
			}
			memset(adsp->protocol[i].inputBuffer, 0, adsp->parms.frames * 2);

			ret = ioctl(adsp->fd, CMD_VIRT_TO_PHYS, &adsp->protocol[i].inPkt);
			if (ret != 0) {
				printf("%s, core[%d] i/o error %d\n", __LINE__, i,  ret);
				goto mmerr;
			}
			printf("INPUT[%d] vaddr:0x%x, paddr:0x%x\n\r", i,
						adsp->protocol[i].inPkt.vaddr,
						adsp->protocol[i].inPkt.paddr);
		}

		if (!adsp->protocol[i].outputBuffer) {
			adsp->protocol[i].outputBuffer= mmap(0,adsp->parms.frames * 2,
						PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, adsp->fd, 0);
			if (!adsp->protocol[i].outputBuffer) {
				printf("%s, core[%d] mmap failed\n", __LINE__, i);
				ret = -ENOMEM;
				goto error;
			}
			memset(adsp->protocol[i].outputBuffer, 0, adsp->parms.frames * 2);

			ret = ioctl(adsp->fd, CMD_VIRT_TO_PHYS, &adsp->protocol[i].outPkt);
			if (ret != 0) {
				printf("%s, core[%d] i/o error %d\n", __LINE__, i,  ret);
				goto mmerr;
			}
			printf("OUTPUT[%d] vaddr:0x%x, paddr:0x%x\n\r", i,
						adsp->protocol[i].outPkt.vaddr,
						adsp->protocol[i].outPkt.paddr);
		}
	}

	printf("FUNC %s OUT\n", __func__);

	return 0;

mmerr:
	for (i = 0; i < 1; i++) {
		if (adsp->protocol[i].inputBuffer)
			munmap(adsp->protocol[i].inputBuffer, adsp->parms.frames * 2);
		if (adsp->protocol[i].outputBuffer)
			munmap(adsp->protocol[i].outputBuffer, adsp->parms.frames * 2);
	}
error:
	close(adsp->fd);
	return ret;
}

static int adsp_close(snd_pcm_extplug_t *ext)
{
	snd_pcm_adsp_t *adsp = ext->private_data;
	int i;

	printf("FUNC %s IN\n", __func__);

	for (i = 0; i < 1; i++) {
		if (adsp->protocol[i].inputBuffer)
			munmap(adsp->protocol[i].inputBuffer, adsp->parms.frames * 2);
		if (adsp->protocol[i].outputBuffer)
			munmap(adsp->protocol[i].outputBuffer, adsp->parms.frames * 2);
	}
	close(adsp->fd);

	printf("FUNC %s OUT\n", __func__);

	return 0;
}

static const snd_pcm_extplug_callback_t adsp_callback = {
	.transfer	= adsp_transfer,
	.init		= adsp_init,
	.close		= adsp_close,
};

static int get_bool_parm(snd_config_t *n, const char *id, const char *str,
			 int *val_ret)
{
	int val;
	if (strcmp(id, str))
		return 0;

	val = snd_config_get_bool(n);
	if (val < 0) {
		SNDERR("Invalid value for %s", id);
		return val;
	}
	*val_ret = val;
	return 1;
}

static int get_int_parm(snd_config_t *n, const char *id, const char *str,
			int *val_ret)
{
	long val;
	int err;

	if (strcmp(id, str))
		return 0;
	err = snd_config_get_integer(n, &val);
	if (err < 0) {
		SNDERR("Invalid value for %s parameter", id);
		return err;
	}
	*val_ret = val;
	return 1;
}

static int get_float_parm(snd_config_t *n, const char *id, const char *str,
			  float *val_ret)
{
	double val;
	int err;

	if (strcmp(id, str))
		return 0;
	err = snd_config_get_ireal(n, &val);
	if (err < 0) {
		SNDERR("Invalid value for %s", id);
		return err;
	}
	*val_ret = val;
	return 1;
}

SND_PCM_PLUGIN_DEFINE_FUNC(adi_dsp)
{
	snd_config_iterator_t i, next;
	snd_pcm_adsp_t *adsp;
	snd_config_t *sconf = NULL;
	const char *device = "/dev/sram_mmap";
	int err;
	struct adsp_parms parms = {
		.core_id = 1,
		.mode = 1,
		.frames =  64,
	};

	printf("SULLIAN ADSP \n");

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 ||
			strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			sconf = n;
			continue;
		}
		if (strcmp(id, "device") == 0) {
			if (snd_config_get_string(n, &device) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		err = get_int_parm(n, id, "core_id", &parms.core_id);
		if (err)
			goto ok;
		err = get_int_parm(n, id, "mode", &parms.mode);
		if (err)
			goto ok;
		err = get_int_parm(n, id, "frames", &parms.frames);
		if (err)
			goto ok;
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	ok:
		if (err < 0)
			return err;
	}

	if(!sconf) {
		SNDERR("No slave configuration for adsp pcm");
		return -EINVAL;
	}

	adsp = calloc(1, sizeof(snd_pcm_adsp_t));
	if (!adsp)
		return -ENOMEM;

	adsp->ext.version		= SND_PCM_EXTPLUG_VERSION;
	adsp->ext.name			= "ADI DSP Plugin";
	adsp->ext.callback		= &adsp_callback;
	adsp->ext.private_data	= adsp;
	adsp->parms				= parms;
	adsp->device			= strdup(device);
	if (adsp->device == NULL) {
		SNDERR("cannot allocate device");
		goto error2;
	}

	err = snd_pcm_extplug_create(&adsp->ext, name, root, sconf, stream, mode);
	if (err < 0)
		goto error1;
#if 0
	snd_pcm_extplug_set_param(&adsp->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 1);
	snd_pcm_extplug_set_slave_param(&adsp->ext,
					SND_PCM_EXTPLUG_HW_CHANNELS, 1);
#endif
	snd_pcm_extplug_set_param(&adsp->ext, SND_PCM_EXTPLUG_HW_FORMAT,
				  SND_PCM_FORMAT_S16);
	snd_pcm_extplug_set_slave_param(&adsp->ext, SND_PCM_EXTPLUG_HW_FORMAT,
					SND_PCM_FORMAT_S16);

	*pcmp = adsp->ext.pcm;
	return 0;

error1:	
	free(adsp->device);
error2:
	free(adsp);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(adi_dsp);
