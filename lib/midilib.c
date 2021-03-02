#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <assert.h>

#include "midilib.h"

unsigned channel_mask = 0xffff;     // bit mask of channels to process


/// Check that we have a specified number of bytes left in the buffer
int check_bufferlen(byte *buffer, byte *ptr, unsigned long len, unsigned long buflen) {

	if ((unsigned) (ptr + len - buffer) > buflen)
		return False;

	return True;
}

/// portable string length
int strlength (const char *str) {
	int i;
	for (i = 0; str[i] != '\0'; ++i);
	return i;
}
/// Match a constant character sequence
int strcompare (const char *buf, const char *match) {

	int len = strlength(match);
	for (int i = 0; i < len; ++i)
		if (buf[i] != match[i]) return False;
	return True;
}

/// Fetch big-endian numbers
uint16_t rev_short (uint16_t val) {

	return ((val & 0xff) << 8) | ((val >> 8) & 0xff);
}
uint32_t rev_long (uint32_t val) {

	return (((rev_short ((uint16_t) val) & 0xffff) << 16) | (rev_short ((uint16_t) (val >> 16)) & 0xffff));
}
unsigned long get_varlen(uint8_t **ptr) { // get a MIDI-style variable length integer

	/* Get a 1-4 byte variable-length value and adjust the pointer past it.
	These are a succession of 7-bit values with a MSB bit of zero marking the end */
	unsigned long val = 0;
	for (int i = 0; i < 4; ++i) {
		byte b = *(*ptr)++;
		val = (val << 7) | (b & 0x7f);
		if (!(b & 0x80))
			return val;
	}
	return val;
}





MIDIFile* midi_load(const char* midifile) {

	FILE *fmid = fopen(midifile, "rb");
	if (!fmid) {
		return NULL;
	}

	MIDIFile *midi = (MIDIFile*)calloc(sizeof(MIDIFile), 1);

	// Read the whole input file into memory
	fseek(fmid, 0, SEEK_END); // find its size
	midi->data_len = ftell(fmid);
	fseek(fmid, 0, SEEK_SET);

	// allocate
	midi->data = (byte *)malloc(midi->data_len + 1);
	long bread = fread(midi->data, midi->data_len, 1, fmid);
	fclose (fmid);

	midi->ticks_per_beat = DEFAULT_BEATTIME;
	memset(midi->channel, 0, sizeof(ChannelStatus) * NUM_CHANNELS);

	return midi;
}

void midi_free(MIDIFile *midi) {

	if (midi->data) free(midi->data);

	free(midi);
}

void midi_writeoutput(MIDIFile *midi, byte msg) {

	// check if there is space in the buffer
	if (midi->output_mem < midi->output_len + 1) {
		midi->output_mem += 512;
		midi->output = (byte*)realloc(midi->output, sizeof(byte) * midi->output_mem);
	}

	midi->output[midi->output_len] = msg;
	midi->output_len++;
}


int midi_process_file_header(MIDIFile *midi) {

	// check if there is enough
	assert(check_bufferlen(midi->data, midi->data, sizeof(MIDIHeader), midi->data_len) == True);

	// get the header
	midi->header = (MIDIHeader*)midi->data;
	assert(strcompare((char *) midi->header->MThd, "MThd") == True); // check that it is a header

	// convert some numbers from big endianess
	midi->num_tracks = rev_short(midi->header->number_of_tracks);
	midi->time_division = rev_short(midi->header->time_division);

	// get the time info
	if (midi->time_division < 0x8000)
		midi->ticks_per_beat = midi->time_division;
	else
		midi->ticks_per_beat = ((midi->time_division >> 8) & 0x7f) /* SMTE frames/sec */ *(midi->time_division & 0xff);     /* ticks/SMTE frame */

	printf ("Header size %" PRId32 "\n", 	rev_long (midi->header->header_size));
	printf ("Format type %d\n", 			rev_short (midi->header->format_type));
	printf ("Number of tracks %d\n", 		midi->num_tracks);
	printf ("Time division %04X\n", 		midi->time_division);
	printf ("Ticks/beat = %d\n", 			midi->ticks_per_beat);

	midi->content = midi->data + rev_long (midi->header->header_size) + 8;   /* point past header to track header, presumably. */
	midi->dataptr = midi->content; // set the running pointer there too

	assert(midi->num_tracks < MAX_TRACKS);
	memset(midi->track, 0, sizeof(TrackStatus) * MAX_TRACKS); // reset the tracks

	return True;
}

int midi_process_track_header(MIDIFile *midi, int tracknum) {

	int result;

	// check that there is enough bytes in the data
	result = check_bufferlen(midi->data, midi->dataptr, sizeof(TrackHeader), midi->data_len); assert(result == True);

	// interpret bytes with the correct structure
	TrackHeader *hdr = (TrackHeader*)midi->dataptr;
	if (!strcompare((char *)(hdr->MTrk), "MTrk")) {
		printf("Missing MTrk[%i] at %p\n", tracknum, midi->dataptr);
		return False;
	}

	// length of the track in bytes
	unsigned long tracklen = rev_long(hdr->track_size);
	printf("\nTrack %d length %ld\n", tracknum, tracklen);

	midi->dataptr += sizeof(TrackHeader); // point past header
	result = check_bufferlen(midi->data, midi->dataptr, tracklen, midi->data_len);
	assert(result == True);

	// set the track pointer to the track content at the current dataptr position
	midi->track[tracknum].trkptr = midi->dataptr;

	midi->dataptr += tracklen; 						// point to the start of the next track
	midi->track[tracknum].trkend = midi->dataptr; 	// the point past the end of the track

	return True;
}

void midi_show_meta(TrackStatus *t, int meta_cmd, int meta_length, char *tag) {

	printf("meta cmd %02X, length %d, %s: \"", meta_cmd, meta_length, tag);
	for (int i = 0; i < meta_length; ++i) {
		int ch = t->trkptr[i];
		printf("%c", isprint (ch) ? ch : '?');
	}
	printf("\"\n");
}

int midi_note_off(TrackStatus *t, int chan) {

	#ifdef DEBUG
	printf("note %d (0x%02X) off, channel %d, volume %d\n", t->note, t->note, chan, t->volume);
	#endif

	// note_off:
	// we're processing this channel and not ignoring percussions...
	if ((1 << chan) & channel_mask && (chan != PERCUSSION_TRACK)) {  // and not ignoring percussion
		t->chan = 0; // force all notes to channel 0 cos we dont really care about ensembles!
		t->cmd = CMD_STOPNOTE;    /* stop processing and return */
		return True;
	}

	return False;
}

int midi_find_next_note(MIDIFile *midi, int tracknum) {

	unsigned long delta_ticks;
	int event, chan;
	int note, velocity, controller, pressure, pitchbend, instrument;
	int meta_cmd, meta_length;
	unsigned long sysex_length;
	char *tag;

	TrackStatus *t = &midi->track[tracknum];	// our track status structure

	while (t->trkptr < t->trkend) { // do until the end of the track

		delta_ticks = get_varlen(&t->trkptr);
		t->time += delta_ticks;

		#ifdef DEBUG
		printf("# trk %d ", tracknum);
		printf("at ticks+%lu=%lu: ", delta_ticks, t->time);
		if (delta_ticks > 0) printf(" [ticks+%-5lu%7lu] ", delta_ticks, t->time);
		else printf(" [ticks+<=0] ");
		#endif

		if (*t->trkptr < 0x80) event = t->last_event;  // using "running status": same event as before
		else event = *t->trkptr++; // otherwise get new "status" (event type) */

		if (event == 0xff) { // meta-event

			meta_cmd = *t->trkptr++;
			meta_length = get_varlen(&t->trkptr);

			#ifdef DEBUG
			printf(" meta event...\n");
			#endif

			if (meta_cmd == 0x51) {

				t->cmd = CMD_TEMPO;
				t->tempo = rev_long (*(uint32_t*)(t->trkptr - 1)) & 0xffffffL;
				#ifdef DEBUG
				printf ("\t SET TEMPO %ld usec/qnote\n", t->tempo);
				#endif

				t->trkptr += meta_length;
				return True;
			}
			t->trkptr += meta_length;
		}
		else if (event < 0x80) {
			printf("Unknown MIDI event type at %p\n", t->trkptr);
			return False;
		}
		else { // all other events

			if (event < 0xf0)
				t->last_event = event;      // remember "running status" if not meta or sysex event
			t->chan = chan = event & 0xf;

			switch (event >> 4) {

			case 0x8: // note off
				t->note = *t->trkptr++;
				t->volume = *t->trkptr++;

				if (midi_note_off(t, chan)) return True;

				break;
			case 0x9: // note on
				t->note = *t->trkptr++;
				t->volume = *t->trkptr++;

				if (t->volume == 0) { // some scores use note-on with zero velocity for off!
					if (midi_note_off(t, chan)) return True;
				}

				#ifdef DEBUG
				printf("note %d (0x%02X) on,  channel %d, volume %d\n", t->note, t->note, chan, t->volume);
				#endif

				// we're processing this channel and not ignoring percussion
				if ((1 << chan) & channel_mask && (chan != PERCUSSION_TRACK)) {
					t->chan = 0; // force all notes to channel 0
					t->cmd = CMD_PLAYNOTE;    /* stop processing and return */
					return True;
				}
				break;
			case 0xa: // key pressure
				note = *t->trkptr++;
				velocity = *t->trkptr++;
				#ifdef DEBUG
				printf("channel %d: note %d (0x%02X) has key pressure %d\n", chan, note, note, velocity);
				#endif
				break;
			case 0xb: // control value change
				controller = *t->trkptr++;
				velocity = *t->trkptr++;
				#ifdef DEBUG
				printf("channel %d: change control value of controller %d to %d\n", chan, controller, velocity);
				#endif
				if (controller == 64) { // PEDALS ADDED BY FELIX
					t->cmd = CMD_PED0;
					t->pedalVals[0] = velocity;
					t->volume = velocity;
					return True;
				} else if (controller == 66) {
					t->cmd = CMD_PED1;
					t->pedalVals[1] = velocity;
					t->volume = velocity;
					return True;
				} else if (controller == 67) {
					t->cmd = CMD_PED2;
					t->pedalVals[2] = velocity;
					t->volume = velocity;
					return True;
				}
				break;
			case 0xc: // program patch, ie which instrument
				instrument = *t->trkptr++;
				midi->channel[chan].instrument = instrument;    // record new instrument for this channel
				#ifdef DEBUG
				printf("channel %d: program patch to instrument %d\n", chan, instrument);
				#endif
				break;
			case 0xd: // channel pressure
				pressure = *t->trkptr++;
				#ifdef DEBUG
				printf("channel %d: after-touch pressure is %d\n", chan, pressure);
				#endif
				break;
			case 0xe: // pitch wheel change
				pitchbend = *t->trkptr++ | (*t->trkptr++ << 7);
				#ifdef DEBUG
				printf("pitch wheel change to %d\n", pitchbend);
				#endif
				break;
			case 0xf: // sysex event
				sysex_length = get_varlen(&t->trkptr);
				#ifdef DEBUG
				printf("SysEx event %d with %ld bytes\n", event, sysex_length);
				#endif
				t->trkptr += sysex_length;
				break;
			default:
				printf("Unknown MIDI command at %p\n", t->trkptr);
				return False;
			}
		}
	}
	t->cmd = CMD_TRACKDONE;   //no more events to process on this track
	++midi->tracks_done;
}

char *describe(NoteInfo *np) { // create a description of a note
	// WARNING: returns a pointer to a static string, so only call once per line, in a printf!
	static char notedescription[100];
	sprintf(notedescription, "at %lu.%03lu msec, note %d (0x%02X) track %d channel %d volume %d instrument %d",
	        np->time_usec / 1000, np->time_usec % 1000, np->note, np->note,
	        np->track, np->channel, np->volume, np->instrument);

	return notedescription;
}


// queue a "note on" or "note off" command
void queue_cmd(MIDIFile *midi, byte cmd, NoteInfo *np) {

	#ifdef DEBUG
	if (cmd == CMD_PLAYNOTE) {
		printf("EN  queue PLAY %s\n", describe(np));
	} else if (cmd == CMD_STOPNOTE) {
		printf("EN  queue STOP %s\n", describe(np));
	} else if (cmd == CMD_PED0 || cmd == CMD_PED1 || cmd == CMD_PED1) {
		printf("EN  queue PED %02X %i\n", cmd, np->volume);
	}
	#endif

	if (midi->queue_numitems == QUEUE_SIZE) pull_queue();
	assert(midi->queue_numitems < QUEUE_SIZE);

	uint32_t horizon = midi->output_usec + midi->output_deficit_usec;
	if (np->time_usec < horizon) { // don't allow revisionist history
		#ifdef DEBUG
		printf("EN  event delayed by %lu usec because queue is too small\n", horizon - np->time_usec);
		#endif

		np->time_usec = horizon;
	}

	int ndx;
	if (midi->queue_numitems == 0) { // queue is empty; restart it
		ndx = midi->queue_oldest_ndx = midi->queue_newest_ndx = midi->queue_numitems = 0;
	}
	else {
		// find a place to insert the new entry in time order
		// this is a stable incremental insertion sort
		ndx = midi->queue_newest_ndx; // start with newest, since we are most often newer
		while (midi->queue[ndx].note.time_usec > np->time_usec) { // search backwards for something as new or older
			if (ndx == midi->queue_oldest_ndx) { // none: we are oldest; add to the start
				if (--midi->queue_oldest_ndx < 0)
					midi->queue_oldest_ndx = QUEUE_SIZE - 1;
				ndx = midi->queue_oldest_ndx;

				midi->queue_numitems++;
				midi->queue[ndx].cmd = cmd;   // fille in the queue entry
				midi->queue[ndx].note = *np;  // structure copy of the note
				return;
			}

			if (--ndx < 0) ndx = QUEUE_SIZE - 1;
		}

		// we are to insert the new item after "ndx", so shift all later entries down, if any
		int from_ndx, to_ndx;
		if (++midi->queue_newest_ndx >= QUEUE_SIZE)
			midi->queue_newest_ndx = 0;
		to_ndx = midi->queue_newest_ndx;
		while (1) {
			if ((from_ndx = to_ndx - 1) < 0) from_ndx = QUEUE_SIZE - 1;
			if (from_ndx == ndx) break;
			midi->queue[to_ndx] = midi->queue[from_ndx]; // structure copy
			to_ndx = from_ndx;
		}
		if (++ndx >= QUEUE_SIZE) ndx = 0;
	}

	// store the item at ndx
	++midi->queue_numitems;
	midi->queue[ndx].cmd = cmd;   // fille in the queue entry
	midi->queue[ndx].note = *np;  // structure copy of the note
}

// output a delay command
void generate_delay(MIDIFile *midi, uint64_t delta_msec) {

	if (delta_msec > 0) {

		assert(delta_msec <= 0x7fff); // "time delta too big"

		#ifdef DEBUG
		if (midi->last_output_was_delay) {
			printf("EN      *** this is a consecutive delay, of %lu msec\n", delta_msec);
		}
		#endif
		midi->last_output_was_delay = true;

		// output a 15-bit delay in big-endian format
		midi_writeoutput(midi, (byte)(delta_msec >> 8));
		midi_writeoutput(midi, (byte)(delta_msec & 0xff));
	}
}




// output all queue elements which are at the oldest time or at most "delaymin" later
void pull_queue(MIDIFile *midi) {

	#ifdef DEBUG
	printf("EN    <-pull from queue at %lu.%03lu msec\n", midi->output_usec / 1000, midi->output_usec % 1000);
	#endif

	uint64_t oldtime = midi->queue[midi->queue_oldest_ndx].note.time_usec; // the oldest time
	assert(oldtime >= midi->output_usec); //, "oldest queue entry goes backward in pull_queue"

	uint64_t delta_usec = (oldtime - midi->output_usec) + midi->output_deficit_usec;
	uint64_t delta_msec = delta_usec / 1000;
	midi->output_deficit_usec = delta_usec % 1000;

	if (delta_usec > 0) { // if time has advanced beyond the merge threshold, output a delay
		
		if (delta_msec > 0) {
			generate_delay(midi, delta_msec);

			#ifdef DEBUG
			printf("EN      at %lu.%03lu msec, delay for %ld msec to %lu.%03lu msec; deficit is %lu usec\n",
				omidi->utput_usec / 1000, midi->output_usec % 1000, delta_msec,
				oldtime / 1000, oldtime % 1000, midi->output_deficit_usec);
			#endif
		}

		#ifdef DEBUG
		printf("EN      at %lu.%03lu msec, a delay of only %lu usec was skipped, and the deficit is now %lu usec\n",
	        midi->output_usec / 1000, midi->output_usec % 1000, oldtime - midi->output_usec, midi->output_deficit_usec);
		#endif
		midi->output_usec = oldtime;
	}

	do {  // output and remove all entries at the same (oldest) time in the queue
		// or which are only delaymin newer
		remove_queue_entry(midi, midi->queue_oldest_ndx);

		if (++midi->queue_oldest_ndx >= QUEUE_SIZE) midi->queue_oldest_ndx = 0;
		--midi->queue_numitems;
	} while(midi->queue_numitems > 0 && midi->queue[midi->queue_oldest_ndx].note.time_usec <= oldtime);

	/*// do any "stop notes" still needed to be generated?
	for (int tgnum = 0; tgnum < num_tonegens; ++tgnum) {

	    struct tonegen_status *tg = &tonegen[tgnum];
	    if (tg->stopnote_pending) { // got one
	        last_output_was_delay = false;
	        if (binaryoutput) {
	            putc(CMD_STOPNOTE | tgnum, outfile);
	            outfile_bytecount += 1;
	        }
	        else {
	            fprintf(outfile, "0x%02X, ", CMD_STOPNOTE | tgnum);
	            outfile_items(1);
	        }
	        if (loggen) fprintf(logfile, "      stop tgen %d %s\n", tgnum, describe(&tg->note));
	        tg->stopnote_pending = false;
	        tg->playing = false;
	    }
	}
	*/
}


void flush_queue(MIDIFile *midi) { // empty the queue

	while (midi->queue_numitems > 0)
		pull_queue();
}


void midi_process_track_data(MIDIFile *midi) {

	unsigned long last_earliest_time = 0;
	int result;

	do { // while there are still track notes to process

		/*
		    Find the track with the earliest event time, and process it's event.

		    A potential improvement: If there are multiple tracks with the same time,
		    first do the ones with STOPNOTE as the next command, if any.  That would
		    help avoid running out of tone generators.  In practice, though, most MIDI
		    files do all the STOPNOTEs first anyway, so it won't have much effect.

		    Usually we start with the track after the one we did last time (tracknum),
		    so that if we run out of tone generators, we have been fair to all the tracks.
		    The alternate "strategy1" says we always start with track 0, which means
		    that we favor early tracks over later ones when there aren't enough tone generators.
		*/

		TrackStatus *trk;
		int count_tracks = midi->num_tracks;
		uint64_t earliest_time = 0x7fffffff; // in ticks, of course
		int tracknum = 0;
		int earliest_tracknum;

		//if (strategy1) tracknum = num_tracks;      /* beyond the end, so we start with track 0 */

		do {
			if (++tracknum >= midi->num_tracks) tracknum = 0;

			trk = &midi->track[tracknum];

			if (trk->cmd != CMD_TRACKDONE && trk->time < earliest_time) {
				earliest_time = trk->time;
				earliest_tracknum = tracknum;
			}
		} while (--count_tracks);

		tracknum = earliest_tracknum;  /* the track we picked */
		trk = &midi->track[tracknum];
		assert(earliest_time >= midi->timenow_ticks); // "time went backwards in process_track_data"


		midi->timenow_ticks = earliest_time; // we make it the global time
		midi->timenow_usec += (uint64_t)(midi->timenow_ticks - midi->timenow_usec_updated) * midi->tempo / midi->ticks_per_beat;
		midi->timenow_usec_updated = midi->timenow_ticks;  // usec version is updated based on the current tempo

		#ifdef DEBUG
		if (earliest_time != last_earliest_time) {
			printf("EN ->process trk %d at time %lu.%03lu msec (%lu ticks)\n", tracknum, midi->timenow_usec / 1000, midi->timenow_usec % 1000, midi->timenow_ticks);
			last_earliest_time = earliest_time;
		}
		#endif

		ChannelStatus *cp = &midi->channel[trk->chan];  // the channel info, if play or stop


		if (trk->cmd == CMD_TEMPO) { // change the global tempo, which affects future usec computations

			if (midi->tempo != trk->tempo) {
				midi->tempo = trk->tempo;
			}

			#ifdef DEBUG
			printf("EN  tempo set to %ld usec/qnote\n", midi->tempo);
			#endif

			result = midi_find_next_note(midi, tracknum); assert(result == True);
		}
		else if (trk->cmd == CMD_STOPNOTE) {

			int ndx;  // find the noteinfo for this note -- which better be playing -- in the channel status
			for (ndx = 0; ndx < MAX_CHANNELNOTES; ++ndx) {
				if (cp->note_playing[ndx] && cp->notes_playing[ndx].note == trk->note && cp->notes_playing[ndx].track == tracknum)
					break;
			}
			if (ndx >= MAX_CHANNELNOTES) { // channel not found... presumably the array overflowed on input

				#ifdef DEBUG
				printf("EN  *** noteinfo slot not found to stop track %d note %d (%02X) channel %d\n", tracknum, trk->note, trk->note, trk->chan);
				#endif
			}
			else { // we found the channel that was paying this note...

				// Analyze the sustain and release parameters. We might generate another "note on"
				// command with reduced volume, and/or move the stopnote command earlier than now.
				NoteInfo *np = &cp->notes_playing[ndx];
				unsigned long duration_usec = midi->timenow_usec - np->time_usec; // it has the start time in it
				unsigned long truncation;
				if (duration_usec <= notemin_usec) truncation = 0;
				else if (duration_usec < releasetime_usec + notemin_usec) truncation = duration_usec - notemin_usec;
				else truncation = releasetime_usec;

				// NOT SURE WHAT IS GOING ON HERE! BUT IT SEEMS TO WORK
				np->time_usec = midi->timenow_usec - truncation; // adjust time to be when the note stops
				queue_cmd(midi, CMD_STOPNOTE, np);
				cp->note_playing[ndx] = false;
			}
			result = midi_find_next_note(midi, tracknum); assert(result == True);
		}
		else if (trk->cmd == CMD_PLAYNOTE) { // Process only one "start note", so other tracks get a chance at tone generators

			int ndx;  // find an unused noteinfo slot to use
			for (ndx = 0; ndx < MAX_CHANNELNOTES; ++ndx) {
				if (!cp->note_playing[ndx])
					break;
			}

			if (ndx >= MAX_CHANNELNOTES) {

				#ifdef DEBUG
				printf("EN  *** no noteinfo slot to queue track %d note %d (%02X) channel %d\n", tracknum, trk->note, trk->note, trk->chan);
				#endif

				//show_noteinfo_slots(tracknum);
			} else {
				cp->note_playing[ndx] = true;  // assign it to us
				NoteInfo *pn = &cp->notes_playing[ndx];
				pn->time_usec = midi->timenow_usec; // fill it in
				pn->track = tracknum;
				pn->channel = trk->chan;
				pn->note = trk->note;
				pn->instrument = cp->instrument;
				pn->volume = trk->volume;
				queue_cmd(midi, CMD_PLAYNOTE, pn);
			}
			result = midi_find_next_note(midi, tracknum); assert(result == True);
		}
		else if (trk->cmd == CMD_PED0) { // PEDAL 0 -- ADDED BY FELIX
			//pedalStatus[0] = trk->pedalVals[0];
			pedalNote.volume = pedalStatus[0];
			pedalNote.time_usec = midi->timenow_usec;
			queue_cmd(midi, CMD_PED0, &pedalNote);
			result = midi_find_next_note(midi, tracknum); assert(result == True);
		}
		else if (trk->cmd == CMD_PED1) { // PEDAL 0 -- ADDED BY FELIX
			pedalStatus[1] = trk->pedalVals[1];
			pedalNote.volume = pedalStatus[1];
			pedalNote.time_usec = midi->timenow_usec;
			queue_cmd(midi, CMD_PED1, &pedalNote);
			result = midi_find_next_note(midi, tracknum); assert(result == True);
		}
		else if (trk->cmd == CMD_PED2) { // PEDAL 0 -- ADDED BY FELIX
			pedalStatus[2] = trk->pedalVals[2];
			pedalNote.volume = midi->pedalStatus[2];
			pedalNote.time_usec = timenow_usec;
			queue_cmd(midi, CMD_PED2, &pedalNote);
			result = midi_find_next_note(midi, tracknum); assert(result == True);
		}
		else {
			printf("BAD CMD in process_track_data"); assert(False);
		}

	} while (midi->tracks_done < num_tracks);

	printf("loop done, now flushing...\n");

	// empty the output queue and generate the end-of-score command
	flush_queue();


	#ifdef DEBUG
	printf("EN ending timenow_usec: %lu.%03lu\n", midi->timenow_usec / 1000, midi->timenow_usec % 1000);
	printf("EN ending output_usec:  %lu.%03lu\n", midi->output_usec / 1000, midi->output_usec % 1000);
	#endif

	assert(midi->timenow_usec >= midi->output_usec); // "time deficit at end of song"
	generate_delay(midi, (midi->timenow_usec - midi->output_usec) / 1000);

	midi_writeoutput(midi, CMD_STOP);
}



int midi_binarize( const char* midifile, const char* outfile) {

	int result;

	MIDIFile *midi = midi_load(midifile);
	result = midi_process_file_header(midi); assert(result == True);

	// initialize for processing of all the tracks
	midi->tempo = DEFAULT_TEMPO;
	midi->tracks_done = 0;

	for (int tracknum = 0; tracknum < midi->num_tracks; ++tracknum) {

		midi->track[tracknum].tempo = DEFAULT_TEMPO;
		midi_process_track_header(midi, tracknum);

		result = midi_find_next_note(midi, tracknum);     /* position to the first note on/off */
		assert(result == True);
	}

	midi->queue_numitems = 0;
	midi->queue_oldest_ndx = 0;
	midi->queue_newest_ndx = 0;
	midi->debugcount = 0;

	midi->last_output_was_delay = false;

	midi->output_len = 0;
	midi->output_mem = 512;
	midi->output = (byte*)calloc(sizeof(byte), midi->output_mem);


	midi->timenow_ticks = 0;
	midi->timenow_usec = 0;
	midi->timenow_usec_updated = 0;
	midi->output_usec = 0;
	midi->output_deficit_usec = 0;
	midi_process_track_data(midi);    // do all the tracks interleaved, like a 1950's multiway merge





	midi_free(midi);
	return 0;
}

