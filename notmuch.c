/* notmuch - Not much of an email program, (just index and search)
 *
 * Copyright © 2009 Carl Worth
 * Copyright © 2009 Keith Packard
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/ .
 *
 * Authors: Carl Worth <cworth@cworth.org>
 *	    Keith Packard <keithp@keithp.com>
 */

#include "notmuch-client.h"

/*
 * Notmuch subcommand hook.
 *
 * The return value will be used as notmuch exit status code,
 * preferrably EXIT_SUCCESS or EXIT_FAILURE.
 */
typedef int (*command_function_t) (notmuch_config_t *config, int argc, char *argv[]);

typedef struct command {
    const char *name;
    command_function_t function;
    notmuch_bool_t create_config;
    const char *summary;
} command_t;

static int
notmuch_help_command (notmuch_config_t *config, int argc, char *argv[]);

static int
notmuch_command (notmuch_config_t *config, int argc, char *argv[]);

static int
_help_for (const char *topic);

static notmuch_bool_t print_version = FALSE, print_help = FALSE;
char *notmuch_requested_db_uuid = NULL;

const notmuch_opt_desc_t notmuch_shared_options [] = {
    { NOTMUCH_OPT_BOOLEAN, &print_version, "version", 'v', 0 },
    { NOTMUCH_OPT_BOOLEAN, &print_help, "help", 'h', 0 },
    { NOTMUCH_OPT_STRING, &notmuch_requested_db_uuid, "uuid", 'u', 0 },
    {0, 0, 0, 0, 0}
};

/* any subcommand wanting to support these options should call
 * inherit notmuch_shared_options and call
 * notmuch_process_shared_options (subcommand_name);
 */
void
notmuch_process_shared_options (const char *subcommand_name) {
    if (print_version) {
	printf ("notmuch " STRINGIFY(NOTMUCH_VERSION) "\n");
	exit (EXIT_SUCCESS);
    }

    if (print_help) {
	int ret = _help_for (subcommand_name);
	exit (ret);
    }
}

/* This is suitable for subcommands that do not actually open the
 * database.
 */
int notmuch_minimal_options (const char *subcommand_name,
				  int argc, char **argv)
{
    int opt_index;

    notmuch_opt_desc_t options[] = {
	{ NOTMUCH_OPT_INHERIT, (void *) &notmuch_shared_options, NULL, 0, 0 },
	{ 0, 0, 0, 0, 0 }
    };

    opt_index = parse_arguments (argc, argv, options, 1);

    if (opt_index < 0)
	return -1;

    /* We can't use argv here as it is sometimes NULL */
    notmuch_process_shared_options (subcommand_name);
    return opt_index;
}

static command_t commands[] = {
    { NULL, notmuch_command, TRUE,
      "Notmuch main command." },
    { "setup", notmuch_setup_command, TRUE,
      "Interactively set up notmuch for first use." },
    { "new", notmuch_new_command, FALSE,
      "Find and import new messages to the notmuch database." },
    { "insert", notmuch_insert_command, FALSE,
      "Add a new message into the maildir and notmuch database." },
    { "search", notmuch_search_command, FALSE,
      "Search for messages matching the given search terms." },
    { "address", notmuch_address_command, FALSE,
      "Get addresses from messages matching the given search terms." },
    { "show", notmuch_show_command, FALSE,
      "Show all messages matching the search terms." },
    { "count", notmuch_count_command, FALSE,
      "Count messages matching the search terms." },
    { "reply", notmuch_reply_command, FALSE,
      "Construct a reply template for a set of messages." },
    { "tag", notmuch_tag_command, FALSE,
      "Add/remove tags for all messages matching the search terms." },
    { "dump", notmuch_dump_command, FALSE,
      "Create a plain-text dump of the tags for each message." },
    { "restore", notmuch_restore_command, FALSE,
      "Restore the tags from the given dump file (see 'dump')." },
    { "compact", notmuch_compact_command, FALSE,
      "Compact the notmuch database." },
    { "config", notmuch_config_command, FALSE,
      "Get or set settings in the notmuch configuration file." },
    { "help", notmuch_help_command, TRUE, /* create but don't save config */
      "This message, or more detailed help for the named command." }
};

typedef struct help_topic {
    const char *name;
    const char *summary;
} help_topic_t;

static help_topic_t help_topics[] = {
    { "search-terms",
      "Common search term syntax." },
    { "hooks",
      "Hooks that will be run before or after certain commands." },
};

static command_t *
find_command (const char *name)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE (commands); i++)
	if ((!name && !commands[i].name) ||
	    (name && commands[i].name && strcmp (name, commands[i].name) == 0))
	    return &commands[i];

    return NULL;
}

int notmuch_format_version;

static void
usage (FILE *out)
{
    command_t *command;
    help_topic_t *topic;
    unsigned int i;

    fprintf (out,
	     "Usage: notmuch --help\n"
	     "       notmuch --version\n"
	     "       notmuch <command> [args...]\n");
    fprintf (out, "\n");
    fprintf (out, "The available commands are as follows:\n");
    fprintf (out, "\n");

    for (i = 0; i < ARRAY_SIZE (commands); i++) {
	command = &commands[i];

	if (command->name)
	    fprintf (out, "  %-12s  %s\n", command->name, command->summary);
    }

    fprintf (out, "\n");
    fprintf (out, "Additional help topics are as follows:\n");
    fprintf (out, "\n");

    for (i = 0; i < ARRAY_SIZE (help_topics); i++) {
	topic = &help_topics[i];
	fprintf (out, "  %-12s  %s\n", topic->name, topic->summary);
    }

    fprintf (out, "\n");
    fprintf (out,
	     "Use \"notmuch help <command or topic>\" for more details "
	     "on each command or topic.\n\n");
}

void
notmuch_exit_if_unsupported_format (void)
{
    if (notmuch_format_version > NOTMUCH_FORMAT_CUR) {
	fprintf (stderr, "\
A caller requested output format version %d, but the installed notmuch\n\
CLI only supports up to format version %d.  You may need to upgrade your\n\
notmuch CLI.\n",
		 notmuch_format_version, NOTMUCH_FORMAT_CUR);
	exit (NOTMUCH_EXIT_FORMAT_TOO_NEW);
    } else if (notmuch_format_version < NOTMUCH_FORMAT_MIN) {
	fprintf (stderr, "\
A caller requested output format version %d, which is no longer supported\n\
by the notmuch CLI (it requires at least version %d).  You may need to\n\
upgrade your notmuch front-end.\n",
		 notmuch_format_version, NOTMUCH_FORMAT_MIN);
	exit (NOTMUCH_EXIT_FORMAT_TOO_OLD);
    } else if (notmuch_format_version < NOTMUCH_FORMAT_MIN_ACTIVE) {
	/* Warn about old version requests so compatibility issues are
	 * less likely when we drop support for a deprecated format
	 * versions. */
	fprintf (stderr, "\
A caller requested deprecated output format version %d, which may not\n\
be supported in the future.\n", notmuch_format_version);
    }
}

void
notmuch_exit_if_unmatched_db_uuid (notmuch_database_t *notmuch)
{
    const char *uuid = NULL;

    if (!notmuch_requested_db_uuid)
	return;
    IGNORE_RESULT (notmuch_database_get_revision (notmuch, &uuid));

    if (strcmp (notmuch_requested_db_uuid, uuid) != 0){
	fprintf (stderr, "Error: requested database revision %s does not match %s\n",
		 notmuch_requested_db_uuid, uuid);
	exit (1);
    }
}

static void
exec_man (const char *page)
{
    if (execlp ("man", "man", page, (char *) NULL)) {
	perror ("exec man");
	exit (1);
    }
}

static int
_help_for (const char *topic_name)
{
    command_t *command;
    help_topic_t *topic;
    unsigned int i;

    if (!topic_name) {
	printf ("The notmuch mail system.\n\n");
	usage (stdout);
	return EXIT_SUCCESS;
    }

    if (strcmp (topic_name, "help") == 0) {
	printf ("The notmuch help system.\n\n"
		"\tNotmuch uses the man command to display help. In case\n"
		"\tof difficulties check that MANPATH includes the pages\n"
		"\tinstalled by notmuch.\n\n"
		"\tTry \"notmuch help\" for a list of topics.\n");
	return EXIT_SUCCESS;
    }

    command = find_command (topic_name);
    if (command) {
	char *page = talloc_asprintf (NULL, "notmuch-%s", command->name);
	exec_man (page);
    }

    for (i = 0; i < ARRAY_SIZE (help_topics); i++) {
	topic = &help_topics[i];
	if (strcmp (topic_name, topic->name) == 0) {
	    char *page = talloc_asprintf (NULL, "notmuch-%s", topic->name);
	    exec_man (page);
	}
    }

    fprintf (stderr,
	     "\nSorry, %s is not a known command. There's not much I can do to help.\n\n",
	     topic_name);
    return EXIT_FAILURE;
}

static int
notmuch_help_command (unused (notmuch_config_t * config), int argc, char *argv[])
{
    int opt_index;

    opt_index = notmuch_minimal_options ("help", argc, argv);
    if (opt_index < 0)
	return EXIT_FAILURE;

    /* skip at least subcommand argument */
    argc-= opt_index;
    argv+= opt_index;

    if (argc == 0) {
	return _help_for (NULL);
    }

    return _help_for (argv[0]);
}

/* Handle the case of "notmuch" being invoked with no command
 * argument. For now we just call notmuch_setup_command, but we plan
 * to be more clever about this in the future.
 */
static int
notmuch_command (notmuch_config_t *config,
		 unused(int argc), unused(char *argv[]))
{
    char *db_path;
    struct stat st;

    /* If the user has never configured notmuch, then run
     * notmuch_setup_command which will give a nice welcome message,
     * and interactively guide the user through the configuration. */
    if (notmuch_config_is_new (config))
	return notmuch_setup_command (config, 0, NULL);

    /* Notmuch is already configured, but is there a database? */
    db_path = talloc_asprintf (config, "%s/%s",
			       notmuch_config_get_database_path (config),
			       ".notmuch");
    if (stat (db_path, &st)) {
	if (errno != ENOENT) {
	    fprintf (stderr, "Error looking for notmuch database at %s: %s\n",
		     db_path, strerror (errno));
	    return EXIT_FAILURE;
	}
	printf ("Notmuch is configured, but there's not yet a database at\n\n\t%s\n\n",
		db_path);
	printf ("You probably want to run \"notmuch new\" now to create that database.\n\n"
		"Note that the first run of \"notmuch new\" can take a very long time\n"
		"and that the resulting database will use roughly the same amount of\n"
		"storage space as the email being indexed.\n\n");
	return EXIT_SUCCESS;
    }

    printf ("Notmuch is configured and appears to have a database. Excellent!\n\n"
	    "At this point you can start exploring the functionality of notmuch by\n"
	    "using commands such as:\n\n"
	    "\tnotmuch search tag:inbox\n\n"
	    "\tnotmuch search to:\"%s\"\n\n"
	    "\tnotmuch search from:\"%s\"\n\n"
	    "\tnotmuch search subject:\"my favorite things\"\n\n"
	    "See \"notmuch help search\" for more details.\n\n"
	    "You can also use \"notmuch show\" with any of the thread IDs resulting\n"
	    "from a search. Finally, you may want to explore using a more sophisticated\n"
	    "interface to notmuch such as the emacs interface implemented in notmuch.el\n"
	    "or any other interface described at http://notmuchmail.org\n\n"
	    "And don't forget to run \"notmuch new\" whenever new mail arrives.\n\n"
	    "Have fun, and may your inbox never have much mail.\n\n",
	    notmuch_config_get_user_name (config),
	    notmuch_config_get_user_primary_email (config));

    return EXIT_SUCCESS;
}

int
main (int argc, char *argv[])
{
    void *local;
    char *talloc_report;
    const char *command_name = NULL;
    command_t *command;
    char *config_file_name = NULL;
    notmuch_config_t *config = NULL;
    int opt_index;
    int ret;

    notmuch_opt_desc_t options[] = {
	{ NOTMUCH_OPT_STRING, &config_file_name, "config", 'c', 0 },
	{ NOTMUCH_OPT_INHERIT, (void *) &notmuch_shared_options, NULL, 0, 0 },
	{ 0, 0, 0, 0, 0 }
    };

    talloc_enable_null_tracking ();

    local = talloc_new (NULL);

    g_mime_init (GMIME_ENABLE_RFC2047_WORKAROUNDS);
#if !GLIB_CHECK_VERSION(2, 35, 1)
    g_type_init ();
#endif

    /* Globally default to the current output format version. */
    notmuch_format_version = NOTMUCH_FORMAT_CUR;

    opt_index = parse_arguments (argc, argv, options, 1);
    if (opt_index < 0) {
	ret = EXIT_FAILURE;
	goto DONE;
    }

    if (opt_index < argc)
	command_name = argv[opt_index];

    notmuch_process_shared_options (command_name);

    command = find_command (command_name);
    if (!command) {
	fprintf (stderr, "Error: Unknown command '%s' (see \"notmuch help\")\n",
		 command_name);
	ret = EXIT_FAILURE;
	goto DONE;
    }

    config = notmuch_config_open (local, config_file_name, command->create_config);
    if (!config) {
	ret = EXIT_FAILURE;
	goto DONE;
    }

    ret = (command->function)(config, argc - opt_index, argv + opt_index);

  DONE:
    if (config)
	notmuch_config_close (config);

    talloc_report = getenv ("NOTMUCH_TALLOC_REPORT");
    if (talloc_report && strcmp (talloc_report, "") != 0) {
	/* this relies on the previous call to
	 * talloc_enable_null_tracking
	 */

	FILE *report = fopen (talloc_report, "w");
	if (report) {
	    talloc_report_full (NULL, report);
	} else {
	    ret = 1;
	    fprintf (stderr, "ERROR: unable to write talloc log. ");
	    perror (talloc_report);
	}
    }

    talloc_free (local);

    return ret;
}
