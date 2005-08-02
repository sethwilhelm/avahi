/* $Id$ */

/***
  This file is part of avahi.
 
  avahi is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.
 
  avahi is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with avahi; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>

#include <avahi-core/core.h>
#include <avahi-core/log.h>

static AvahiEntryGroup *group = NULL;
static AvahiServer *server = NULL;
static gchar *service_name = NULL;

static gboolean quit_timeout(gpointer data) {
    g_main_loop_quit(data);
    return FALSE;
}

static void dump_line(const gchar *text, gpointer userdata) {
    printf("%s\n", text);
}

static gboolean dump_timeout(gpointer data) {
    AvahiServer *Avahi = data;
    avahi_server_dump(Avahi, dump_line, NULL);
    return TRUE;
}

static void record_browser_callback(AvahiRecordBrowser *r, gint interface, guchar protocol, AvahiBrowserEvent event, AvahiRecord *record, gpointer userdata) {
    gchar *t;
    
    g_assert(r);
    g_assert(record);
    g_assert(interface > 0);
    g_assert(protocol != AVAHI_PROTO_UNSPEC);

    avahi_log_debug("SUBSCRIPTION: record [%s] on %i.%i is %s", t = avahi_record_to_string(record), interface, protocol,
              event == AVAHI_BROWSER_NEW ? "new" : "remove");

    g_free(t);
}

static void remove_entries(void);
static void create_entries(gboolean new_name);

static void entry_group_callback(AvahiServer *s, AvahiEntryGroup *g, AvahiEntryGroupState state, gpointer userdata) {
    avahi_log_debug("entry group state: %i", state); 

    if (state == AVAHI_ENTRY_GROUP_COLLISION) {
        remove_entries();
        create_entries(TRUE);
        avahi_log_debug("Service name conflict, retrying with <%s>", service_name);
    } else if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        avahi_log_debug("Service established under name <%s>", service_name);
    }
}

static void server_callback(AvahiServer *s, AvahiServerState state, gpointer userdata) {

     avahi_log_debug("server state: %i", state); 
    
    if (state == AVAHI_SERVER_RUNNING) {
        avahi_log_debug("Server startup complete.  Host name is <%s>", avahi_server_get_host_name_fqdn(s));
        create_entries(FALSE);
    } else if (state == AVAHI_SERVER_COLLISION) {
        gchar *n;
        remove_entries();

        n = avahi_alternative_host_name(avahi_server_get_host_name(s));

        avahi_log_debug("Host name conflict, retrying with <%s>", n);
        avahi_server_set_host_name(s, n);
        g_free(n);
    }
}

static void remove_entries(void) {
    if (group)
        avahi_entry_group_free(group);

    group = NULL;
}

static void create_entries(gboolean new_name) {
    AvahiAddress a;
    remove_entries();
    
    group = avahi_entry_group_new(server, entry_group_callback, NULL);   
    
    if (!service_name)
        service_name = g_strdup("Test Service");
    else if (new_name) {
        gchar *n = avahi_alternative_service_name(service_name);
        g_free(service_name);
        service_name = n;
    }
    
    if (avahi_server_add_service(server, group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, service_name, "_http._tcp", NULL, NULL, 80, "foo", NULL) < 0) {
        avahi_log_error("Failed to add HTTP service");
        goto fail;
    }

    if (avahi_server_add_service(server, group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, service_name, "_ftp._tcp", NULL, NULL, 21, "foo", NULL) < 0) {
        avahi_log_error("Failed to add FTP service");
        goto fail;
    }

    if (avahi_server_add_service(server, group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, service_name, "_webdav._tcp", NULL, NULL, 80, "foo", NULL) < 0) {
        avahi_log_error("Failed to add WEBDAV service");
        goto fail;
    }

    if (avahi_server_add_dns_server_address(server, group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, NULL, AVAHI_DNS_SERVER_RESOLVE, avahi_address_parse("192.168.50.1", AVAHI_PROTO_UNSPEC, &a), 53) < 0) {
        avahi_log_error("Failed to add new DNS Server address");
        goto fail;
    }

    avahi_entry_group_commit(group);
    return;

fail:
    if (group)
        avahi_entry_group_free(group);

    group = NULL;
}

static void hnr_callback(AvahiHostNameResolver *r, gint iface, guchar protocol, AvahiBrowserEvent event, const gchar *hostname, const AvahiAddress *a, gpointer userdata) {
    gchar t[64];

    if (a)
        avahi_address_snprint(t, sizeof(t), a);

    avahi_log_debug("HNR: (%i.%i) <%s> -> %s [%s]", iface, protocol, hostname, a ? t : "n/a", event == AVAHI_RESOLVER_FOUND ? "found" : "timeout");
}

static void ar_callback(AvahiAddressResolver *r, gint iface, guchar protocol, AvahiBrowserEvent event, const AvahiAddress *a, const gchar *hostname, gpointer userdata) {
    gchar t[64];

    avahi_address_snprint(t, sizeof(t), a);

    avahi_log_debug("AR: (%i.%i) %s -> <%s> [%s]", iface, protocol, t, hostname ? hostname : "n/a", event == AVAHI_RESOLVER_FOUND ? "found" : "timeout");
}

static void db_callback(AvahiDomainBrowser *b, gint iface, guchar protocol, AvahiBrowserEvent event, const gchar *domain, gpointer userdata) {

    avahi_log_debug("DB: (%i.%i) <%s> [%s]", iface, protocol, domain, event == AVAHI_BROWSER_NEW ? "new" : "remove");
}

static void stb_callback(AvahiServiceTypeBrowser *b, gint iface, guchar protocol, AvahiBrowserEvent event, const gchar *service_type, const gchar *domain, gpointer userdata) {

    avahi_log_debug("STB: (%i.%i) %s in <%s> [%s]", iface, protocol, service_type, domain, event == AVAHI_BROWSER_NEW ? "new" : "remove");
}

static void sb_callback(AvahiServiceBrowser *b, gint iface, guchar protocol, AvahiBrowserEvent event, const gchar *name, const gchar *service_type, const gchar *domain, gpointer userdata) {
   avahi_log_debug("SB: (%i.%i) <%s> as %s in <%s> [%s]", iface, protocol, name, service_type, domain, event == AVAHI_BROWSER_NEW ? "new" : "remove");
}

static void sr_callback(AvahiServiceResolver *r, gint iface, guchar protocol, AvahiBrowserEvent event, const gchar *name, const gchar*service_type, const gchar*domain_name, const gchar*hostname, const AvahiAddress *a, guint16 port, AvahiStringList *txt, gpointer userdata) {

    if (event == AVAHI_RESOLVER_TIMEOUT)
        avahi_log_debug("SR: (%i.%i) <%s> as %s in <%s> [timeout]", iface, protocol, name, service_type, domain_name);
    else {
        gchar t[64], *s;
        
        avahi_address_snprint(t, sizeof(t), a);

        s = avahi_string_list_to_string(txt);
        avahi_log_debug("SR: (%i.%i) <%s> as %s in <%s>: %s/%s:%i (%s) [found]", iface, protocol, name, service_type, domain_name, hostname, t, port, s);
        g_free(s);
    }
}

static void dsb_callback(AvahiDNSServerBrowser *b, gint iface, guchar protocol, AvahiBrowserEvent event, const gchar*hostname, const AvahiAddress *a, guint16 port, gpointer userdata) {
    gchar t[64];
    avahi_address_snprint(t, sizeof(t), a);
    avahi_log_debug("DSB: (%i.%i): %s/%s:%i [%s]", iface, protocol, hostname, t, port, event == AVAHI_BROWSER_NEW ? "new" : "remove");
}

int main(int argc, char *argv[]) {
    GMainLoop *loop = NULL;
    AvahiRecordBrowser *r;
    AvahiHostNameResolver *hnr;
    AvahiAddressResolver *ar;
    AvahiKey *k;
    AvahiServerConfig config;
    AvahiAddress a;
    AvahiDomainBrowser *db;
    AvahiServiceTypeBrowser *stb;
    AvahiServiceBrowser *sb;
    AvahiServiceResolver *sr;
    AvahiDNSServerBrowser *dsb;
    
    avahi_server_config_init(&config);
/*     config.host_name = g_strdup("test"); */
    server = avahi_server_new(NULL, &config, server_callback, NULL);
    avahi_server_config_free(&config);

    k = avahi_key_new("_http._tcp.local", AVAHI_DNS_CLASS_IN, AVAHI_DNS_TYPE_PTR);
    r = avahi_record_browser_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, k, record_browser_callback, NULL);
    avahi_key_unref(k);

    hnr = avahi_host_name_resolver_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "codes-CompUTER.local", AVAHI_PROTO_UNSPEC, hnr_callback, NULL);

    ar = avahi_address_resolver_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, avahi_address_parse("192.168.50.15", AVAHI_PROTO_INET, &a), ar_callback, NULL);

    db = avahi_domain_browser_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, NULL, AVAHI_DOMAIN_BROWSER_BROWSE, db_callback, NULL);

    stb = avahi_service_type_browser_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, NULL, stb_callback, NULL);

    sb = avahi_service_browser_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_http._tcp", NULL, sb_callback, NULL);

    sr = avahi_service_resolver_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "Ecstasy HTTP", "_http._tcp", "local", AVAHI_PROTO_UNSPEC, sr_callback, NULL);

    dsb = avahi_dns_server_browser_new(server, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "local", AVAHI_DNS_SERVER_RESOLVE, AVAHI_PROTO_UNSPEC, dsb_callback, NULL);

    
    g_timeout_add(1000*5, dump_timeout, server);
    g_timeout_add(1000*60, quit_timeout, loop);     
    
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    avahi_record_browser_free(r);
    avahi_host_name_resolver_free(hnr);
    avahi_address_resolver_free(ar);
    avahi_service_type_browser_free(stb);
    avahi_service_browser_free(sb);
    avahi_service_resolver_free(sr);
    avahi_dns_server_browser_free(dsb);

    if (group)
        avahi_entry_group_free(group);   

    if (server)
        avahi_server_free(server);

    g_free(service_name);
    
    return 0;
}
