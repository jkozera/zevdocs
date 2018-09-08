/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * This file is part of Devhelp.
 *
 * Copyright (C) 2001-2003 CodeFactory AB
 * Copyright (C) 2001-2008 Imendio AB
 *
 * Devhelp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * Devhelp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Devhelp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <locale.h>
#include <glib/gi18n.h>
#include <devhelp/devhelp.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "dh-app.h"
#include "dh-settings-app.h"


static void wait_for_core ()
{
        bool connected = FALSE;
        struct sockaddr_in addr;
        int sockfd;

        while (!connected) {
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                addr.sin_family = AF_INET;
                addr.sin_port = htons(12340);
                inet_aton("127.0.0.1", &addr.sin_addr.s_addr);

                if (connect(sockfd, &addr, sizeof(addr)) < 0) {
                        printf("Waiting for zealcore...\n");
                } else {
                        connected = TRUE;
                }
                sleep(1);
        }
}

int
main (int argc, char **argv)
{
        g_autoptr(DhApp) application;
        gint status;
        int pid_status;
        pid_t zealcore_pid = fork();

        if (zealcore_pid == 0) {
                char env[10000] = {0};
                strcat(env, "/run/host/usr/share:");
                strcat(env, getenv("HOME"));
                strcat(env, "/.local/share");
                setenv("XDG_DATA_DIRS", env, 1);
                execl("/app/bin/zealcore", "zealcore", (char*) NULL);
                return 0;
        }

        wait_for_core();

        setlocale (LC_ALL, "");
        textdomain (GETTEXT_PACKAGE);

        dh_init ();

        application = dh_app_new ();
        status = g_application_run (G_APPLICATION (application), argc, argv);

        dh_finalize ();
        dh_settings_app_unref_singleton ();

        if (waitpid(zealcore_pid, &pid_status, WNOHANG) == 0) {
                kill(zealcore_pid, SIGTERM);
        }

        return status;
}
