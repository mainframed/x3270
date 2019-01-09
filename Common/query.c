/*
 * Copyright (c) 1993-2019 Paul Mattes.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of Paul Mattes nor the names of his contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *      query.c
 *              The Query() action.
 */

#include "globals.h"

#include "appres.h"

#include "actions.h"
#include "charset.h"
#include "copyright.h"
#include "ctlrc.h"
#include "host.h"
#include "lazya.h"
#include "popups.h"
#include "query.h"
#include "telnet.h"
#include "task.h"
#include "trace.h"
#include "unicodec.h"
#include "utf8.h"
#include "utils.h"
#include "varbuf.h"

/* Globals */

/* Statics */

static const char *
query_terminal_name(void)
{
    return (appres.termname != NULL)? appres.termname: full_model_name;
}

static const char *
query_build(void)
{
    return build;
}

static const char *
get_connect_time(void)
{
    time_t t, td;
    long dy, hr, mn, sc;

    if (!CONNECTED) {
	return NULL;
    }

    (void) time(&t);

    td = t - ns_time;
    dy = (long)(td / (3600 * 24));
    hr = (dy % (3600 * 24)) / 3600;
    mn = (td % 3600) / 60;
    sc = td % 60;

    return (dy > 0)?
	lazyaf("%ud%02u:%02u:%02u", dy, hr, mn, sc):
	lazyaf("%02u:%02u:%02u", hr, mn, sc);
}

static const char *
get_codepage(void)
{
    char *sbcs = lazyaf("%s sbcs gcsgid %u cpgid %u",
	    get_canonical_codepage(),
	    (unsigned short)((cgcsgid >> 16) & 0xffff),
	    (unsigned short)(cgcsgid & 0xffff));
    return dbcs? lazyaf("%s dbcs gcsgid %u cpgid %u",
	    sbcs,
	    (unsigned short)((cgcsgid_dbcs >> 16) & 0xffff),
	    (unsigned short)(cgcsgid_dbcs & 0xffff)):
	sbcs;
}

static const char *
get_codepages(void)
{
    csname_t *c = get_csnames();
    varbuf_t r;
    int i, j;
    char *sep = "";

    vb_init(&r);
    for (i = 0; c[i].name != NULL; i++) {
	vb_appendf(&r, "%s%s %cbcs", sep, c[i].name, c[i].dbcs? 'd': 's');
	sep = "\n";
	for (j = 0; j < c[i].num_aliases; j++) {
	    vb_appendf(&r, " %s", c[i].aliases[j]);
	}
    }

    free_csnames(c);
    return lazya(vb_consume(&r));
}

static const char *
get_proxy(void)
{
    const char *ptype = net_proxy_type();

    return (ptype != NULL)?
	lazyaf("%s %s %s", ptype, net_proxy_host(), net_proxy_port()):
	NULL;
}

static const char *
get_rx(void)
{
    if (!CONNECTED) {
	return NULL;
    }

    return IN_3270?
	lazyaf("records %u bytes %u", ns_rrcvd, ns_brcvd):
	lazyaf("bytes %u", ns_brcvd);
}

static const char *
get_screentracefile(void)
{
    if (!toggled(SCREEN_TRACE)) {
	return NULL;
    }
    return trace_get_screentrace_name();
}

static const char *
get_tasks(void)
{
    char *r = task_get_tasks();
    size_t sl = strlen(r);

    if (sl > 0 && r[sl - 1] == '\n') {
	r[sl - 1] = '\0';
    }
    return lazya(r);
}

static const char *
get_tracefile(void)
{
    if (!toggled(TRACING)) {
	return NULL;
    }
    return tracefile_name;
}

static const char *
get_tx(void)
{
    if (!CONNECTED) {
	return NULL;
    }

    return IN_3270?
	lazyaf("records %u bytes %u", ns_rsent, ns_bsent):
	lazyaf("bytes %u", ns_bsent);
}

bool
Query_action(ia_t ia, unsigned argc, const char **argv)
{
    static struct {
	char *name;
	const char *(*fn)(void);
	const char *string;
	bool hidden;
	bool specific;
    } queries[] = {
	{ "Actions", all_actions, NULL, false, true },
	{ "BindPluName", net_query_bind_plu_name, NULL, false, false },
	{ "BuildOptions", build_options, NULL, false, false },
	{ "ConnectionState", host_query_connection_state, NULL, false, false },
	{ "ConnectTime", get_connect_time, NULL, false, false },
	{ "CodePage", get_codepage, NULL, false, false },
	{ "CodePages", get_codepages, NULL, false, true },
	{ "Copyright", show_copyright, NULL, false, true },
	{ "Cursor", ctlr_query_cursor, NULL, true, false },
	{ "Cursor1", ctlr_query_cursor1, NULL, false, false },
	{ "Formatted", ctlr_query_formatted, NULL, false, false },
	{ "Host", net_query_host, NULL, false, false },
	{ "LocalEncoding", get_codeset, NULL, false, false },
	{ "LuName", net_query_lu_name, NULL, false, false },
	{ "Model", NULL, full_model_name, true, false },
	{ "Proxy", get_proxy, NULL, false, false },
	{ "ScreenSizeCurrent", ctlr_query_cur_size, NULL, false, false },
	{ "ScreenSizeMax", ctlr_query_max_size, NULL, false, false },
	{ "ScreenTraceFile", get_screentracefile, NULL, false, false },
	{ "Ssl", net_query_tls, NULL, true, false },
	{ "StatsRx", get_rx, NULL, false, false },
	{ "StatsTx", get_tx, NULL, false, false },
	{ "Tasks", get_tasks, NULL, false, true },
	{ "TelnetMyOptions", net_myopts, NULL, false, false },
	{ "TelnetHostOptions", net_hisopts, NULL, false, false },
	{ "TerminalName", query_terminal_name, NULL, false, false },
	{ "TraceFile", get_tracefile, NULL, false, false },
	{ "Tls", net_query_tls, NULL, false, false },
	{ "TlsCertInfo", net_server_cert_info, NULL, false, true },
	{ "TlsProvider", net_sio_provider, NULL, false, false },
	{ "TlsSessionInfo", net_session_info, NULL, false, true },
	{ "Tn3270eOptions", tn3270e_current_opts, NULL, false, false },
	{ "Version", query_build, NULL, false, false },
	{ NULL, NULL, false, false }
    };
    int i;
    size_t sl;

    action_debug("Query", ia, argc, argv);
    if (check_argc("Query", argc, 0, 1) < 0) {
	return false;
    }

    switch (argc) {
    case 0:
	for (i = 0; queries[i].name != NULL; i++) {
	    if (!queries[i].hidden) {
		const char *s = (queries[i].fn? (*queries[i].fn)():
			queries[i].string);

		if (s == NULL) {
		    s = "";
		}
		if (queries[i].specific && strcmp(s, "")) {
		    s = "...";
		}
		action_output("%s:%s%s", queries[i].name, *s? " ": "", s);
	    }
	}
	break;
    case 1:
	sl = strlen(argv[0]);
	for (i = 0; queries[i].name != NULL; i++) {
	    if (!strncasecmp(argv[0], queries[i].name, sl)) {
		const char *s;

		if (strlen(queries[i].name) > sl &&
			queries[i + 1].name != NULL &&
			!strncasecmp(argv[0], queries[i + 1].name, sl)) {
		    popup_an_error("Query: Ambiguous parameter");
		    return false;
		}

		if (queries[i].fn) {
		    s = (*queries[i].fn)();
		} else {
		    s = queries[i].string;
		}
		if (s == NULL) {
		    s = "";
		}
		action_output("%s\n", s);
		return true;
	    }
	}
	popup_an_error("Query: Unknown parameter");
	return false;
    }
    return true;
}

/**
 * Query module registration.
 */
void
query_register(void)
{
    static action_table_t actions[] = {
	{ "Query",		Query_action, 0 }
    };

    /* Register actions.*/
    register_actions(actions, array_count(actions));
}
