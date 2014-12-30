/**
 * @file callscript.c  Interactive menu
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2014 Edvina AB, Sollentuna, Sweden
 * 
 * Based on the menu module
 */
#define DEBUG 1

/*** Call Scripts
 *
 * Directory
 *	Configuration parameter: callscript_dir
 *			sets the default script directory. If not set
 *			the configuration directory will be used.
 */
#include <time.h>
#include <re.h>
#include <baresip.h>
#include <strings.h>
#include "callscript.h"


/** Defines the status modes */
enum statmode {
	STATMODE_CALL = 0,
	STATMODE_OFF,
};


static uint64_t start_ticks;          /**< Ticks when app started         */
static time_t start_time;             /**< Start time of application      */
static struct tmr tmr_alert;          /**< Incoming call alert timer      */
static struct tmr tmr_stat;           /**< Call status timer              */
static enum statmode statmode;        /**< Status mode                    */
static struct mbuf *dialbuf;          /**< Buffer for dialled number      */
static struct le *le_cur;             /**< Current User-Agent (struct ua) */
static char script_dir[256];	      /**< Default directory for scripts */
static int callactive;		      /**< Do we have incoming call */

static int cs_cmd_quit(struct re_printf *pf, const char *data, const size_t datalen, const int line);
static int cs_cmd_wait(struct re_printf *pf, const char *data, const size_t datalen, const int line);
static int cs_cmd_verbose(struct re_printf *pf, const char *data, const size_t datalen, const int line);

struct s_script_commands {
	enum sc_type type;		/**< Command ENUM */
	const char *cmd;		/**< Command in text form */
	int requires_arg;		/**< True if command requires argument */
	/*! Function for working with the command */
	int (*exec)(struct re_printf *pf, const char *data, const size_t datalen, const int line);

} script_commands[] = {
	{ VERBOSE, "verbose", 1, &cs_cmd_verbose },
	{ WAIT, "wait", 1, &cs_cmd_wait },
	{ QUIT, "quit", 0, &cs_cmd_quit },
};

/*! \brief Find command if it exists 
 * \return Pointer to structure if match is found
 */
static struct s_script_commands *find_command(char *cmd) 
{
	int i = sizeof(script_commands) / sizeof(struct s_script_commands);
	int j;
	for (j = 0; j < i; j++) {
		if (!strcasecmp(script_commands[j].cmd, cmd)) {
#ifdef DEBUG
			re_fprintf(stdout, "    ### Command %s matches existing command\n", cmd);
#endif
			return &script_commands[j];
		}
	}
	return NULL;
}

/*! \brief Verbose: Log to console
 *
 * Syntax: verbose <text>
 */
static int cs_cmd_verbose(struct re_printf *pf, const char *data, const size_t datalen, const int line)
{
	int err;

	err = re_hprintf(pf, "Verbose: %s\n", data);

        return err;
}

/*! \brief Wait for a while 
 *
 * Syntax: wait <sec>
 *         Waits a number of seconds
 */
static int cs_cmd_wait(struct re_printf *pf, const char *data, const size_t datalen, const int line)
{
	int err;

	err = re_hprintf(pf, "Wait\n");

	//Wait

        return err;
}

/*! \brief Quit application, go back to command line. All fine */
static int cs_cmd_quit(struct re_printf *pf, const char *data, const size_t datalen, const int line)
{
	int err = 0;

	err = re_hprintf(pf, "Quit\n");

	/* This does not work */
        ua_stop_all(false);

        return err;
}


/*! \brief Parse a call script line 
 *
 * 	SYNTAX: WORD ARG,ARG,ARG
 *	Whitespace or '#' in the first position is a comment
 *	Whitespace between WORD and first ARG is ignored
 *
 */
static int cs_parse_line(struct re_printf *pf, const int line, char *linebuffer)
{
	char *arg = NULL;
	char *cmd = NULL;
	int err = 0;
	struct s_script_commands *command;

	if (linebuffer[0]) {
		if (linebuffer[0] == ' ' || linebuffer[0] == '#') {
			/* This is a comment */
			return 0;	/* Line ignored */
		}
	}
	
	cmd = strsep(&linebuffer, " \t\n");
	arg = linebuffer;
#ifdef DEBUG
	re_hprintf(pf, "   - Parsing line: %d : Cmd %s Arg %s Linebuffer %s\n", line, cmd, arg, linebuffer);
#endif
	if (cmd != NULL && cmd[0] != '\0') {
		command = find_command(cmd);
		if (command != NULL) {
#ifdef DEBUG
			re_hprintf(pf, "   - Found registred command in line: %d : Cmd %s\n", line, cmd);
#endif
			err = (command->exec)(pf, arg, strlen(arg), line);
		} else {
			re_hprintf(pf, "   - Error: Unknown command %s\n", cmd);
		}
	}

	return(err);
}


/*! \brief Read the call script file and parse it */
static int callscript_read(struct re_printf *pf, const char *filename)
{
	FILE *fp;
	char fullfilename[256];
	char linebuffer[256];
	int err = 0;
	int line=0;

	/* If we have a script directory and the script file name is not a full path
	   use the default directory */
	if (script_dir[0] != '\0' && filename && filename[0] != '/') {
		snprintf(fullfilename, sizeof(fullfilename), "%s/%s", script_dir, filename);
	} else {
		snprintf(fullfilename, sizeof(fullfilename), "%s", filename);
	}

	if (!(fp = fopen(fullfilename, "r"))) {
		re_hprintf(pf, "Error: Call Script File failed to open: %s\n", fullfilename);
		return err;
	}
#ifdef DEBUG
	re_fprintf(stderr, "\nCall Script File open for reading: %s\n", fullfilename);
#endif
	/* Ready to parse a file and have fun */
	while (fgets(linebuffer, sizeof(linebuffer), fp) != NULL) {
		line++;
#ifdef DEBUG
		re_hprintf(pf, "DEBUG: %d - %s\n", line, linebuffer);
#endif
		cs_parse_line(pf, line, linebuffer);
	}
	if (feof(fp)) {
		re_hprintf(pf, "DEBUG: --EOF %d lines\n", line);
	} else {
		re_hprintf(pf, "DEBUG:     --- File read error %d\n", ferror(fp));
	}
	fclose(fp);
	return err;
}

static int callscript_start(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	(void)pf;

	//info("%r: Call Script : %s\n", "Hej hej");
	err |= re_hprintf(pf, "\n--- Call Script: File name %s\n", carg->prm);


	if (str_isset(carg->prm)) {
		callscript_read(pf, carg->prm);
	}

	return err;
}

static const struct cmd cmdv[] = {
	{'X',       CMD_PRM, "Call Script exec",          callscript_start },
};

static void alert_start(void *arg)
{
	(void)arg;

	ui_output("\033[10;1000]\033[11;1000]\a");

	tmr_start(&tmr_alert, 1000, alert_start, NULL);
}


static void alert_stop(void)
{
	if (tmr_isrunning(&tmr_alert))
		ui_output("\r");

	tmr_cancel(&tmr_alert);
}


static void message_handler(const struct pl *peer, const struct pl *ctype,
			    struct mbuf *body, void *arg)
{
	(void)ctype;
	(void)arg;

	(void)re_fprintf(stderr, "\r%r: \"%b\"\n", peer,
			 mbuf_buf(body), mbuf_get_left(body));

	(void)play_file(NULL, "message.wav", 0);
}

static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	(void)call;
	(void)prm;
	(void)arg;

	switch (ev) {

	case UA_EVENT_CALL_INCOMING:
		info("CallScript %s: Starting script on incoming call from: %s %s -",
		     ua_aor(ua), call_peername(call), call_peeruri(call));
		callactive++;
		alert_start(0);
		break;

	case UA_EVENT_CALL_ESTABLISHED:
	case UA_EVENT_CALL_CLOSED:
		callactive--;
		alert_stop();
		break;

	case UA_EVENT_REGISTER_OK:
		info("Callscript: Registration ok");
		//NOOP
		break;

	case UA_EVENT_UNREGISTERING:
		return;

	default:
		break;
	}

}

static int module_init(void)
{
	int err;
	int rerr;

	callactive = 0;

	dialbuf = mbuf_alloc(64);
	if (!dialbuf)
		return ENOMEM;

	start_ticks = tmr_jiffies();
	(void)time(&start_time);
	tmr_init(&tmr_alert);
	statmode = STATMODE_CALL;

	err  = cmd_register(cmdv, ARRAY_SIZE(cmdv));
	err |= uag_event_register(ua_event_handler, NULL);

	err |= message_init(message_handler, NULL);
	script_dir[0] = '\0';

	(void)conf_get_str(conf_cur(), "callscript_dir", script_dir, sizeof(script_dir));

	if (script_dir[0] == '\0') {
		rerr = conf_path_get(script_dir, sizeof(script_dir));
        	if (!rerr) {
               		warning("account: conf_path_get (%m)\n", err);
                	return err;
		}
        }

	if (script_dir[0] != '\0') {
		re_fprintf(stdout, "\nCall Script Directory: %s\n", script_dir);
	}


	return err;
}


static int module_close(void)
{
	message_close();
	uag_event_unregister(ua_event_handler);
	cmd_unregister(cmdv);

	tmr_cancel(&tmr_alert);
	tmr_cancel(&tmr_stat);
	dialbuf = mem_deref(dialbuf);

	le_cur = NULL;
	callactive = 0;

	return 0;
}


const struct mod_export DECL_EXPORTS(menu) = {
	"menu",
	"application",
	module_init,
	module_close
};
