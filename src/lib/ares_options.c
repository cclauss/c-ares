/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) 2008 Daniel Stenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ares_setup.h"

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif

#include "ares.h"
#include "ares_data.h"
#include "ares_inet_net_pton.h"
#include "ares_private.h"

void ares_destroy_options(struct ares_options *options)
{
  int i;

  ares_free(options->servers);

  for (i = 0; options->domains && i < options->ndomains; i++) {
    ares_free(options->domains[i]);
  }

  ares_free(options->domains);
  ares_free(options->sortlist);
  ares_free(options->lookups);
  ares_free(options->resolvconf_path);
  ares_free(options->hosts_path);
}

static struct in_addr *ares_save_opt_servers(ares_channel_t *channel,
                                             int            *nservers)
{
  ares__slist_node_t *snode;
  struct in_addr     *out =
    ares_malloc_zero(ares__slist_len(channel->servers) * sizeof(*out));

  *nservers = 0;

  if (out == NULL) {
    return NULL;
  }

  for (snode = ares__slist_node_first(channel->servers); snode != NULL;
       snode = ares__slist_node_next(snode)) {
    const struct server_state *server = ares__slist_node_val(snode);

    if (server->addr.family != AF_INET) {
      continue;
    }

    memcpy(&out[*nservers], &server->addr.addr.addr4, sizeof(*out));
    (*nservers)++;
  }

  return out;
}

/* Save options from initialized channel */
int ares_save_options(ares_channel_t *channel, struct ares_options *options,
                      int *optmask)
{
  size_t i;

  /* NOTE: We can't zero the whole thing out, this is because the size of the
   *       struct ares_options changes over time, so if someone compiled
   *       with an older version, their struct size might be smaller and
   *       we might overwrite their memory! So using the optmask is critical
   *       here, as they could have only set options they knew about.
   *
   *       Unfortunately ares_destroy_options() doesn't take an optmask, so
   *       there are a few pointers we *must* zero out otherwise we won't
   *       know if they were allocated or not
   */
  options->servers         = NULL;
  options->domains         = NULL;
  options->sortlist        = NULL;
  options->lookups         = NULL;
  options->resolvconf_path = NULL;
  options->hosts_path      = NULL;

  if (!ARES_CONFIG_CHECK(channel)) {
    return ARES_ENODATA;
  }

  if (channel->optmask & ARES_OPT_FLAGS) {
    options->flags = (int)channel->flags;
  }

  /* We convert ARES_OPT_TIMEOUT to ARES_OPT_TIMEOUTMS in
   * ares__init_by_options() */
  if (channel->optmask & ARES_OPT_TIMEOUTMS) {
    options->timeout = (int)channel->timeout;
  }

  if (channel->optmask & ARES_OPT_TRIES) {
    options->tries = (int)channel->tries;
  }

  if (channel->optmask & ARES_OPT_NDOTS) {
    options->ndots = (int)channel->ndots;
  }

  if (channel->optmask & ARES_OPT_MAXTIMEOUTMS) {
    options->maxtimeout = (int)channel->maxtimeout;
  }

  if (channel->optmask & ARES_OPT_UDP_PORT) {
    options->udp_port = ntohs(channel->udp_port);
  }
  if (channel->optmask & ARES_OPT_TCP_PORT) {
    options->tcp_port = ntohs(channel->tcp_port);
  }

  if (channel->optmask & ARES_OPT_SOCK_STATE_CB) {
    options->sock_state_cb      = channel->sock_state_cb;
    options->sock_state_cb_data = channel->sock_state_cb_data;
  }

  if (channel->optmask & ARES_OPT_SERVERS) {
    options->servers = ares_save_opt_servers(channel, &options->nservers);
    if (options->servers == NULL) {
      return ARES_ENOMEM;
    }
  }

  if (channel->optmask & ARES_OPT_DOMAINS) {
    options->domains = NULL;
    if (channel->ndomains) {
      options->domains = ares_malloc(channel->ndomains * sizeof(char *));
      if (!options->domains) {
        return ARES_ENOMEM;
      }

      for (i = 0; i < channel->ndomains; i++) {
        options->domains[i] = ares_strdup(channel->domains[i]);
        if (!options->domains[i]) {
          options->ndomains = (int)i;
          return ARES_ENOMEM;
        }
      }
    }
    options->ndomains = (int)channel->ndomains;
  }

  if (channel->optmask & ARES_OPT_LOOKUPS) {
    options->lookups = ares_strdup(channel->lookups);
    if (!options->lookups && channel->lookups) {
      return ARES_ENOMEM;
    }
  }

  if (channel->optmask & ARES_OPT_SORTLIST) {
    options->sortlist = NULL;
    if (channel->nsort) {
      options->sortlist = ares_malloc(channel->nsort * sizeof(struct apattern));
      if (!options->sortlist) {
        return ARES_ENOMEM;
      }
      for (i = 0; i < channel->nsort; i++) {
        options->sortlist[i] = channel->sortlist[i];
      }
    }
    options->nsort = (int)channel->nsort;
  }

  if (channel->optmask & ARES_OPT_RESOLVCONF) {
    options->resolvconf_path = ares_strdup(channel->resolvconf_path);
    if (!options->resolvconf_path) {
      return ARES_ENOMEM;
    }
  }

  if (channel->optmask & ARES_OPT_HOSTS_FILE) {
    options->hosts_path = ares_strdup(channel->hosts_path);
    if (!options->hosts_path) {
      return ARES_ENOMEM;
    }
  }

  if (channel->optmask & ARES_OPT_SOCK_SNDBUF &&
      channel->socket_send_buffer_size > 0) {
    options->socket_send_buffer_size = channel->socket_send_buffer_size;
  }

  if (channel->optmask & ARES_OPT_SOCK_RCVBUF &&
      channel->socket_receive_buffer_size > 0) {
    options->socket_receive_buffer_size = channel->socket_receive_buffer_size;
  }

  if (channel->optmask & ARES_OPT_EDNSPSZ) {
    options->ednspsz = (int)channel->ednspsz;
  }

  if (channel->optmask & ARES_OPT_UDP_MAX_QUERIES) {
    options->udp_max_queries = (int)channel->udp_max_queries;
  }

  if (channel->optmask & ARES_OPT_QUERY_CACHE) {
    options->qcache_max_ttl = channel->qcache_max_ttl;
  }

  *optmask = (int)channel->optmask;

  return ARES_SUCCESS;
}

static ares_status_t ares__init_options_servers(ares_channel_t       *channel,
                                                const struct in_addr *servers,
                                                size_t                nservers)
{
  ares__llist_t *slist = NULL;
  ares_status_t  status;

  status = ares_in_addr_to_server_config_llist(servers, nservers, &slist);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares__servers_update(channel, slist, ARES_TRUE);

  ares__llist_destroy(slist);

  return status;
}

ares_status_t ares__init_by_options(ares_channel_t            *channel,
                                    const struct ares_options *options,
                                    int                        optmask)
{
  size_t i;

  if (channel == NULL) {
    return ARES_ENODATA;
  }

  if (options == NULL) {
    if (optmask != 0) {
      return ARES_ENODATA;
    }
    return ARES_SUCCESS;
  }

  /* Easy stuff. */
  if (optmask & ARES_OPT_FLAGS) {
    channel->flags = (unsigned int)options->flags;
  }

  if (optmask & ARES_OPT_TIMEOUTMS) {
    channel->timeout = (unsigned int)options->timeout;
  } else if (optmask & ARES_OPT_TIMEOUT) {
    /* Convert to milliseconds */
    optmask          |= ARES_OPT_TIMEOUTMS;
    optmask          &= ~(ARES_OPT_TIMEOUT);
    channel->timeout  = (unsigned int)options->timeout * 1000;
  }

  if (optmask & ARES_OPT_TRIES) {
    channel->tries = (size_t)options->tries;
  }

  if (optmask & ARES_OPT_NDOTS) {
    channel->ndots = (size_t)options->ndots;
  }

  if (optmask & ARES_OPT_MAXTIMEOUTMS) {
    channel->maxtimeout = (size_t)options->maxtimeout;
  }

  if (optmask & ARES_OPT_ROTATE) {
    channel->rotate = ARES_TRUE;
  }

  if (optmask & ARES_OPT_NOROTATE) {
    channel->rotate = ARES_FALSE;
  }

  if (optmask & ARES_OPT_UDP_PORT) {
    channel->udp_port = htons(options->udp_port);
  }

  if (optmask & ARES_OPT_TCP_PORT) {
    channel->tcp_port = htons(options->tcp_port);
  }

  if (optmask & ARES_OPT_SOCK_STATE_CB) {
    channel->sock_state_cb      = options->sock_state_cb;
    channel->sock_state_cb_data = options->sock_state_cb_data;
  }

  if (optmask & ARES_OPT_SOCK_SNDBUF && options->socket_send_buffer_size > 0) {
    channel->socket_send_buffer_size = options->socket_send_buffer_size;
  }

  if (optmask & ARES_OPT_SOCK_RCVBUF &&
      options->socket_receive_buffer_size > 0) {
    channel->socket_receive_buffer_size = options->socket_receive_buffer_size;
  }

  if (optmask & ARES_OPT_EDNSPSZ) {
    channel->ednspsz = (size_t)options->ednspsz;
  }

  /* Copy the domains, if given.  Keep channel->ndomains consistent so
   * we can clean up in case of error.
   */
  if (optmask & ARES_OPT_DOMAINS && options->ndomains > 0) {
    channel->domains =
      ares_malloc_zero((size_t)options->ndomains * sizeof(char *));
    if (!channel->domains) {
      return ARES_ENOMEM;
    }
    channel->ndomains = (size_t)options->ndomains;
    for (i = 0; i < (size_t)options->ndomains; i++) {
      channel->domains[i] = ares_strdup(options->domains[i]);
      if (!channel->domains[i]) {
        return ARES_ENOMEM;
      }
    }
  }

  /* Set lookups, if given. */
  if (optmask & ARES_OPT_LOOKUPS) {
    channel->lookups = ares_strdup(options->lookups);
    if (!channel->lookups) {
      return ARES_ENOMEM;
    }
  }

  /* copy sortlist */
  if (optmask & ARES_OPT_SORTLIST && options->nsort > 0) {
    channel->nsort = (size_t)options->nsort;
    channel->sortlist =
      ares_malloc((size_t)options->nsort * sizeof(struct apattern));
    if (!channel->sortlist) {
      return ARES_ENOMEM;
    }
    for (i = 0; i < (size_t)options->nsort; i++) {
      channel->sortlist[i] = options->sortlist[i];
    }
  }

  /* Set path for resolv.conf file, if given. */
  if (optmask & ARES_OPT_RESOLVCONF) {
    channel->resolvconf_path = ares_strdup(options->resolvconf_path);
    if (!channel->resolvconf_path && options->resolvconf_path) {
      return ARES_ENOMEM;
    }
  }

  /* Set path for hosts file, if given. */
  if (optmask & ARES_OPT_HOSTS_FILE) {
    channel->hosts_path = ares_strdup(options->hosts_path);
    if (!channel->hosts_path && options->hosts_path) {
      return ARES_ENOMEM;
    }
  }

  if (optmask & ARES_OPT_UDP_MAX_QUERIES) {
    channel->udp_max_queries = (size_t)options->udp_max_queries;
  }

  if (optmask & ARES_OPT_QUERY_CACHE) {
    channel->qcache_max_ttl = options->qcache_max_ttl;
  }

  /* Initialize the ipv4 servers if provided */
  if (optmask & ARES_OPT_SERVERS && options->nservers > 0) {
    ares_status_t status;
    status = ares__init_options_servers(channel, options->servers,
                                        (size_t)options->nservers);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  channel->optmask = (unsigned int)optmask;

  return ARES_SUCCESS;
}
