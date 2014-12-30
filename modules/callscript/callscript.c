/**
 * @file callscript.c  Interactive menu
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2014 Edvina AB, Sollentuna, Sweden
 * 
 * Based on the menu module
 */

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

/*! \brief Read the call script file and parse it */
static int callscript_read(struct re_printf *pf, const char *filename)
{
	FILE *fp;
	char fullfilename[256];
	int err = 0;

	/* If we have a script directory and the script file name is not a full path
	   use the default directory */
	if (script_dir[0] != '\0' && filename && filename[0] != '/') {
		snprintf(fullfilename, sizeof(fullfilename), "%s/%s", script_dir, filename);
	} else {
		snprintf(fullfilename, sizeof(fullfilename), "%s", filename);
	}

	if (!(fp = fopen(fullfilename, "r"))) {
		re_fprintf(stderr, "\nCall Script File failed to open: %s\n", fullfilename);
		return err;
	}
	re_fprintf(stderr, "\nCall Script File open for reading: %s\n", fullfilename);
	/* Ready to parse a file and have fun */
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
		info("%s: Incoming call from: %s %s -"
		     " (press ENTER to accept)\n",
		     ua_aor(ua), call_peername(call), call_peeruri(call));
		alert_start(0);
		break;

	case UA_EVENT_CALL_ESTABLISHED:
	case UA_EVENT_CALL_CLOSED:
		alert_stop();
		break;

	case UA_EVENT_REGISTER_OK:
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

	return 0;
}


const struct mod_export DECL_EXPORTS(menu) = {
	"menu",
	"application",
	module_init,
	module_close
};
