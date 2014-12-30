/**
 * @file callscript.h  Call scripting
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2014 Edvina AB, Sollentuna, Sweden
 * 
 * Based on the menu module
 */

enum sc_type {
	RING,		/* Change to ring status */
	PLAY,		/* Play media file */
	LOOPPLAY,	/* Play media file, loop */
	WAIT,		/* Wait some time */
	ANSWER,		/* Answer call */
	HANGUP,		/* Hangup call */
	SET,		/* Set various properties for the call */
	LOG,		/* Log message to syslog */
	QUIT,		/* Exit application */
	VERBOSE,	/* Log to console */
	NOOP		/* Do nothing */
	
};

struct scriptentry {
	int	prio;
	enum sc_type type;
	int	value;
	char 	*arg;
	struct scriptentry *next;
};
