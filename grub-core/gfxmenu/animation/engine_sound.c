/* engine_sound.c - Motherboard speaker play sound.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright 2015,2017 Ruyi Boy - All Rights Reserved
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/speaker.h>
#include <grub/engine_sound.h>

/*
 * Tested in July 2015.
 * In some public places, or a quiet place, whether it will disturb others?
 */

 /* Chinese folk songs: Jasmine Flower  */
static grub_uint16_t default_start[] =
  { 659, 0, 659, 784, 880, 1046, 1046, 880, 784, 0, 784, 880, 784, 0, 0, 0 };

/* Tokyo Hot Theme Song :)
static grub_uint16_t default_start[] =
  { 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 440, 523, 659, 784, 880, 784, 659, 523, 494, 587, 740, 880, 988, 880, 740, 587, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 440, 523, 659, 784, 880, 784, 659, 523, 494, 587, 740, 880, 988, 880, 740, 587, 659, 659, 659, 659, 784, 784, 659, 659, 784, 784, 988, 988, 0, 0, 659, 659, 784, 784, 659, 659, 784, 784, 988, 988, 0, 0, 988, 988, 659, 659, 784, 784, 0, 0, 659, 659, 784, 784, 659, 659, 784, 784, 988, 988, 0, 0, 659, 659, 784, 784, 659, 659, 784, 784, 988, 988, 0, 0, 988, 988, 659, 659, 784, 784, 0, 0, 659, 659, 784, 784, 440, 440, 0, 0, 523, 523, 659, 659, 784, 784, 0, 0, 740, 740, 880, 880, 740, 740, 0, 0, 740, 740, 587, 587, 740, 740, 0, 0, 659, 659, 784, 784, 659, 659, 784, 784, 988, 988, 0, 0, 659, 659, 784, 784, 659, 659, 784, 784, 988, 988, 0, 0, 988, 988, 659, 659, 784, 784, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 1047, 880, 880, 880, 880, 880, 880, 880, 880, 880, 880, 880, 880, 880, 880, 880, 880, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 494, 370, 494, 622, 740, 494, 622, 740, 988, 622, 740, 988, 1245, 740, 988, 1245, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 831, 659, 494, 415, 659, 494, 415, 330, 494, 415, 330, 247, 415, 330, 247, 330, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 330, 208, 247, 330, 415, 247, 330, 415, 494, 330, 415, 494, 659, 415, 494, 659, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 831, 659, 494, 415, 659, 494, 415, 330, 494, 415, 330, 247, 415, 330, 247, 330, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1109, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 1245, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 988, 784, 784, 784, 784, 784, 784, 784, 784, 784, 784, 784, 784, 784, 784, 784, 784, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 1175, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 440, 523, 659, 784, 880, 784, 659, 523, 494, 587, 740, 880, 988, 880, 740, 587, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 784, 784, 784, 784, 784, 784, 784, 740, 740, 740, 784, 784, 587, 587, 659, 659, 440, 523, 659, 784, 880, 784, 659, 523, 494, 587, 740, 880, 988, 880, 740, 587, 392, 392, 392, 392, 392, 392, 392, 392 };
  */

/* Change option's sound.  */
static grub_uint16_t default_select[] =
  { 587, 262 };

sound_class_t
engine_sound_new (void)
{
  sound_class_t sound;
  sound = grub_malloc (sizeof(*sound));
  if (!sound)
	{
	  return 0;
	}

  /* Parse sound file, to be continued.  */
  sound->start_buf = 0;
  sound->start_len = sizeof(default_start) / sizeof(grub_uint16_t);

  sound->select_buf = 0;
  sound->select_len = sizeof(default_select) / sizeof(grub_uint16_t);

  sound->cur_index = 0;
  sound->play_mark = ENGINE_SOUND_PLAY;

  return sound;
}

void
engine_player_refresh (int is_selected, int cur_sound, void *data)
{
  sound_class_t sound = data;

  if (sound->selected != is_selected)
	{
	  sound->selected = is_selected;
	  sound->cur_index = 0;
	  sound->play_mark = ENGINE_SOUND_PLAY;
	}

  if (sound->play_mark)
	{
	  int i = sound->cur_index;
	  int len = 0;
	  grub_uint16_t cur_pitch = 0;

	  switch (cur_sound)
		{
		case ENGINE_START_SOUND:
		  len = sound->start_len;
		  cur_pitch = default_start[i];
		  break;

		case ENGINE_SELECT_SOUND:
		  len = sound->select_len;
		  cur_pitch = default_select[i];
		  break;
		}

	  if (!cur_pitch)
		{
		  grub_speaker_beep_off ();
		}
	  else
		{
		  grub_speaker_beep_on (cur_pitch);
		}

	  sound->cur_index++;
	  if (sound->cur_index > len)
		{
		  grub_speaker_beep_off ();
		  sound->cur_index = 0;
		  sound->play_mark = ENGINE_SOUND_STOP;
		}
	}
}

void
engine_sound_destroy (sound_class_t sound)
{
  /* grub_free (sound->start_buf);
   grub_free (sound->select_buf); */
  grub_speaker_beep_off ();
  grub_free (sound);
}
