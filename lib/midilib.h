
#ifndef MIDILIB
#define MIDILIB


#define VERSION "1.0"
#define True 1
#define False 0

#define DEFAULT_TEMPO 500000L   // the MIDI-specified default tempo in usec/beat 
#define DEFAULT_BEATTIME 240    // the MIDI-specified default ticks per beat 

#define MAX_TRACKS 24           // max number of MIDI tracks we will process
#define PERCUSSION_TRACK 9      // the track MIDI uses for percussion sounds

#define NUM_CHANNELS 16         // MIDI-specified number of channels
#define MAX_CHANNELNOTES 24     // max number of notes playing simultaneously on a channel

#define notemin_usec 250   		// minimum note time in usec after the release is deducted
#define releasetime_usec 0 		// release time in usec for silence at the end of notes


#define QUEUE_SIZE 100 			// maximum number of note play/stop commands we queue


// output bytestream commands, which are also stored in track_status.cmd *******
#define CMD_PLAYNOTE    0x90    /* play a note: low nibble is generator #, note is next byte */
#define CMD_STOPNOTE    0x80    /* stop a note: low nibble is generator # */
#define CMD_INSTRUMENT  0xc0    /* change instrument; low nibble is generator #, instrument is next byte */
#define CMD_RESTART     0xe0    /* restart the score from the beginning */
#define CMD_STOP        0xf0    /* stop playing */
#define CMD_PED0        0xa0    /* control 64 -- damper pedal (sustain) */ /* PEDAL ADDED BY FELIX */
#define CMD_PED1        0xb0    /* control 66 -- sostenuto pedal */
#define CMD_PED2        0xd0    /* control 67 -- soft pedal*/

/* the following other commands are stored in the track_status.com */
#define CMD_TEMPO       0xFE    /* tempo in usec per quarter note ("beat") */
#define CMD_TRACKDONE   0xFF    /* no more data left in this track */

/* ***************************************************** */


typedef unsigned char byte;
typedef uint64_t timestamp;  // see note about this in the queuing routines
// WARNING: timestamp was uint32!!!


/***********  MIDI file header formats  *****************/

typedef struct midi_header MIDIHeader;
struct midi_header {

	int8_t MThd[4];
	uint32_t header_size;
	uint16_t format_type;
	uint16_t number_of_tracks;
	uint16_t time_division;
};

typedef struct track_header TrackHeader;
struct track_header {

	int8_t MTrk[4];
	uint32_t track_size;
};

/* ***************************************************** */


/// current status of a MIDI track
typedef struct track_status TrackStatus;
struct track_status {

	uint8_t *trkptr;             // ptr to the next event we care about
	uint8_t *trkend;             // ptr just past the end of the track
	unsigned long time;          // what time we're at in the score, in ticks
	unsigned long tempo;         // the last tempo set on this track
	int preferred_tonegen;       // for strategy2: try to use this generator
	byte cmd;                    // next CMD_xxxx event coming up
	byte chan, note, volume;     // if it is CMD_PLAYNOTE or CMD_STOPNOTE, the note info
	byte last_event;             // the last event, for MIDI's "running status"
	byte pedalVals[3];           // ADDED BY FELIX for PEDALS
};


/// everything we might care about as a note plays
typedef struct noteinfo NoteInfo;
struct noteinfo {                   
	timestamp time_usec;             // when it starts or stops, in absolute usec since song start
	int track, channel, note, instrument, volume; // all the nitty-gritty about it
};


/// current status of a channel
typedef struct channel_status ChannelStatus;
struct channel_status {          

	int 		instrument;               			// which instrument this channel currently plays
	bool 		note_playing[MAX_CHANNELNOTES]; 	// slots for notes that are playing on this channel
	NoteInfo 	notes_playing[MAX_CHANNELNOTES]; 	// information about them
};


typedef struct queue_entry QEntry;
struct queue_entry {      // the format of each queue entry
	
	byte cmd;              // CMD_PLAY or CMD_STOP
	struct noteinfo note;  // info about the note, including the action time
};



typedef struct MIDIFile MIDIFile;
struct MIDIFile {

	byte 		*data;
	byte		*content; 			// pointer to data after header
	byte 		*dataptr; 			// used as a runaway pointer in the data
	long 		data_len;

	MIDIHeader 	*header;
	uint16_t 	num_tracks;

	TrackStatus track[MAX_TRACKS];
	ChannelStatus channel[NUM_CHANNELS];

	int 		tracks_done;
	uint64_t 	timenow_ticks;			// the current processing time in ticks
	uint64_t 	timenow_usec; 			// the current processing time in usec
	uint64_t 	timenow_usec_updated;   // when, in ticks, we last updated timenow_usec using the current tempo
	uint32_t 	output_usec;			// the time we last output, in usec
	uint32_t 	output_deficit_usec; 	// the leftover usec < 1000 still to be used for a "delay"


	uint32_t 	time_division;
	uint32_t 	ticks_per_beat;
	uint64_t 	tempo;				// current global tempo in usec/beat


	QEntry 		queue[QUEUE_SIZE];
	int 		queue_numitems;
	int 		queue_oldest_ndx;
	int 		queue_newest_ndx;
	int 		debugcount;

	bool 		last_output_was_delay;

	byte 		*output;
	uint32_t 	output_len;		// how much of the output space is used
	uint32_t 	output_mem;		// how much space is allocated for the output
};



int midi_binarize( const char* midifile, const char* outfile);




#endif
