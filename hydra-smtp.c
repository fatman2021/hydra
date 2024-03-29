#include "hydra-mod.h"
#include "sasl.h"

extern char *HYDRA_EXIT;
char *buf;

int smtp_auth_mechanism = AUTH_LOGIN;

char *smtp_read_server_capacity(int sock) {
  char *ptr = NULL;
  int resp = 0;
  char *buf = NULL;

  do {
    if (buf != NULL)
      free(buf);
    ptr = buf = hydra_receive_line(sock);
    if (buf != NULL) {
      if (isdigit((int) buf[0]) && buf[3] == ' ')
        resp = 1;
      else {
        if (buf[strlen(buf) - 1] == '\n')
          buf[strlen(buf) - 1] = 0;
        if (buf[strlen(buf) - 1] == '\r')
          buf[strlen(buf) - 1] = 0;
        if ((ptr = rindex(buf, '\n')) != NULL) {
          ptr++;
          if (isdigit((int) *ptr) && *(ptr + 3) == ' ')
            resp = 1;
        }
      }
    }
  } while (buf != NULL && resp == 0);
  return buf;
}

int start_smtp(int s, char *ip, int port, unsigned char options, char *miscptr, FILE * fp) {
  char *empty = "";
  char *login, *pass, buffer[500], buffer2[500];

  if (strlen(login = hydra_get_next_login()) == 0)
    login = empty;
  if (strlen(pass = hydra_get_next_password()) == 0)
    pass = empty;

  while (hydra_data_ready(s) > 0) {
    if ((buf = hydra_receive_line(s)) == NULL)
      return (1);
    free(buf);
  }

  switch (smtp_auth_mechanism) {

  case AUTH_PLAIN:
    sprintf(buffer, "AUTH PLAIN\r\n");
    if (hydra_send(s, buffer, strlen(buffer), 0) < 0) {
      return 1;
    }
    if ((buf = hydra_receive_line(s)) == NULL)
      return 1;
    if (strstr(buf, "334") == NULL) {
      hydra_report(stderr, "[ERROR] SMTP PLAIN AUTH : %s\n", buf);
      free(buf);
      return 3;
    }
    free(buf);

    memset(buffer, 0, sizeof(buffer));
    sasl_plain(buffer, login, pass);
    sprintf(buffer, "%.250s\r\n", buffer);
    break;

#ifdef LIBOPENSSLNEW
  case AUTH_CRAMMD5:{
      int rc = 0;
      char *preplogin;

      rc = sasl_saslprep(login, SASL_ALLOW_UNASSIGNED, &preplogin);
      if (rc) {
        return 3;
      }

      sprintf(buffer, "AUTH CRAM-MD5\r\n");
      if (hydra_send(s, buffer, strlen(buffer), 0) < 0) {
        return 1;
      }
      //get the one-time BASE64 encoded challenge
      if ((buf = hydra_receive_line(s)) == NULL)
        return 1;
      if (strstr(buf, "334") == NULL) {
        hydra_report(stderr, "[ERROR] SMTP CRAM-MD5 AUTH : %s\n", buf);
        free(buf);
        return 3;
      }
      memset(buffer, 0, sizeof(buffer));
      from64tobits((char *) buffer, buf + 4);
      free(buf);

      memset(buffer2, 0, sizeof(buffer2));
      sasl_cram_md5(buffer2, pass, buffer);

      sprintf(buffer, "%s %.250s", preplogin, buffer2);
      hydra_tobase64((unsigned char *) buffer, strlen(buffer), sizeof(buffer));
      sprintf(buffer, "%.250s\r\n", buffer);
      free(preplogin);
    }
    break;

  case AUTH_DIGESTMD5:{
      sprintf(buffer, "AUTH DIGEST-MD5\r\n");

      if (hydra_send(s, buffer, strlen(buffer), 0) < 0)
        return 1;
      //receive
      if ((buf = hydra_receive_line(s)) == NULL)
        return 1;
      if (strstr(buf, "334") == NULL) {
        hydra_report(stderr, "[ERROR] SMTP DIGEST-MD5 AUTH : %s\n", buf);
        free(buf);
        return 3;
      }
      memset(buffer, 0, sizeof(buffer));
      from64tobits((char *) buffer, buf + 4);
      free(buf);

      if (verbose)
        hydra_report(stderr, "DEBUG S: %s\n", buffer);

      sasl_digest_md5(buffer2, login, pass, buffer, miscptr, "smtp", NULL, 0, NULL);
      if (buffer2 == NULL)
        return 3;

      if (verbose)
        hydra_report(stderr, "DEBUG C: %s\n", buffer2);
      hydra_tobase64((unsigned char *) buffer2, strlen(buffer2), sizeof(buffer2));
      sprintf(buffer, "%s\r\n", buffer2);
    }
    break;
#endif

  case AUTH_NTLM:{
      unsigned char buf1[4096];
      unsigned char buf2[4096];

      //send auth and receive challenge
      buildAuthRequest((tSmbNtlmAuthRequest *) buf2, 0, NULL, NULL);
      to64frombits(buf1, buf2, SmbLength((tSmbNtlmAuthRequest *) buf2));
      sprintf(buffer, "AUTH NTLM %s\r\n", buf1);
      if (hydra_send(s, buffer, strlen(buffer), 0) < 0) {
        return 1;
      }
      if ((buf = hydra_receive_line(s)) == NULL)
        return 1;
      if (strstr(buf, "334") == NULL) {
        hydra_report(stderr, "[ERROR] SMTP NTLM AUTH : %s\n", buf);
        free(buf);
        return 3;
      }
      //recover challenge
      from64tobits((char *) buf1, buf + 4);
      free(buf);

      buildAuthResponse((tSmbNtlmAuthChallenge *) buf1, (tSmbNtlmAuthResponse *) buf2, 0, login, pass, NULL, NULL);
      to64frombits(buf1, buf2, SmbLength((tSmbNtlmAuthResponse *) buf2));
      sprintf(buffer, "%s\r\n", buf1);
    }
    break;

  default:
    /* by default trying AUTH LOGIN */
    sprintf(buffer, "AUTH LOGIN\r\n");
    if (hydra_send(s, buffer, strlen(buffer), 0) < 0) {
      return 1;
    }
    if ((buf = hydra_receive_line(s)) == NULL)
      return 1;

    /* 504 5.7.4 Unrecognized authentication type  */
    if (strstr(buf, "334") == NULL) {
      hydra_report(stderr, "[ERROR] SMTP LOGIN AUTH, either this auth is disabled\nor server is not using auth: %s\n", buf);
      free(buf);
      return 3;
    }
    free(buf);
    sprintf(buffer2, "%.250s", login);
    hydra_tobase64((unsigned char *) buffer2, strlen(buffer2), sizeof(buffer2));
    sprintf(buffer, "%.250s\r\n", buffer2);

    if (hydra_send(s, buffer, strlen(buffer), 0) < 0) {
      return 1;
    }
    if ((buf = hydra_receive_line(s)) == NULL)
      return (1);
    if (strstr(buf, "334") == NULL) {
      hydra_report(stderr, "[ERROR] SMTP LOGIN AUTH : %s\n", buf);
      free(buf);
      return (3);
    }
    free(buf);

    sprintf(buffer2, "%.250s", pass);
    hydra_tobase64((unsigned char *) buffer2, strlen(buffer2), sizeof(buffer2));
    sprintf(buffer, "%.250s\r\n", buffer2);
  }

  if (hydra_send(s, buffer, strlen(buffer), 0) < 0) {
    return 1;
  }
  if ((buf = hydra_receive_line(s)) == NULL)
    return (1);

#ifdef LIBOPENSSLNEW
  if (smtp_auth_mechanism == AUTH_DIGESTMD5) {
    if (strstr(buf, "334") != NULL) {
      memset(buffer2, 0, sizeof(buffer2));
      from64tobits((char *) buffer2, buf + 4);
      if (strstr(buffer2, "rspauth=") != NULL) {
        hydra_report_found_host(port, ip, "smtp", fp);
        hydra_completed_pair_found();
        free(buf);
        if (memcmp(hydra_get_next_pair(), &HYDRA_EXIT, sizeof(HYDRA_EXIT)) == 0)
          return 3;
        return 1;
      }
    }
  } else
#endif
  {
    if (strstr(buf, "235") != NULL) {
      hydra_report_found_host(port, ip, "smtp", fp);
      hydra_completed_pair_found();
      free(buf);
      if (memcmp(hydra_get_next_pair(), &HYDRA_EXIT, sizeof(HYDRA_EXIT)) == 0)
        return 3;
      return 1;
    }
  }
  free(buf);
  hydra_completed_pair();
  if (memcmp(hydra_get_next_pair(), &HYDRA_EXIT, sizeof(HYDRA_EXIT)) == 0)
    return 3;

  return 2;
}

void service_smtp(char *ip, int sp, unsigned char options, char *miscptr, FILE * fp, int port) {
  int run = 1, next_run = 1, sock = -1, i = 0;
  int myport = PORT_SMTP, mysslport = PORT_SMTP_SSL, disable_tls = 0;

  char *buffer1 = "EHLO hydra\r\n";
  char *buffer2 = "HELO hydra\r\n";

  hydra_register_socket(sp);
  if (memcmp(hydra_get_next_pair(), &HYDRA_EXIT, sizeof(HYDRA_EXIT)) == 0)
    return;
  while (1) {
    switch (run) {
    case 1:                    /* connect and service init function */
      if (sock >= 0)
        sock = hydra_disconnect(sock);
      if ((options & OPTION_SSL) == 0) {
        if (port != 0)
          myport = port;
        sock = hydra_connect_tcp(ip, myport);
        port = myport;
      } else {
        if (port != 0)
          mysslport = port;
        sock = hydra_connect_ssl(ip, mysslport);
        port = myport;
      }
      if (sock < 0) {
        if (verbose || debug)
          hydra_report(stderr, "[ERROR] Child with pid %d terminating, can not connect\n", (int) getpid());
        hydra_child_exit(1);
      }

      /* receive initial header */
      if ((buf = hydra_receive_line(sock)) == NULL)
        hydra_child_exit(2);
      if (strstr(buf, "220") == NULL) {
        hydra_report(stderr, "[WARNING] SMTP does not allow to connect: %s\n", buf);
        free(buf);
        hydra_child_exit(2);
      }
      while (strstr(buf, "220 ") == NULL) {
        free(buf);
        buf = hydra_receive_line(sock);
      }
      free(buf);

      /* send ehlo and receive/ignore reply */
      if (hydra_send(sock, buffer1, strlen(buffer1), 0) < 0)
        hydra_child_exit(2);

      buf = smtp_read_server_capacity(sock);
      if (buf == NULL)
        hydra_child_exit(2);

#ifdef LIBOPENSSLNEW      
      if (!disable_tls) {
	/* if we got a positive answer */
	if (buf[0] == '2') {
          if (strstr(buf, "STARTTLS") != NULL) {
            hydra_send(sock, "STARTTLS\r\n", strlen("STARTTLS\r\n"), 0);
            free(buf);
            buf = hydra_receive_line(sock);
            if (buf[0] != '2') {
              hydra_report(stderr, "[ERROR] TLS negotiation failed, no answer received from STARTTLS request\n");
            } else {
              free(buf);
              if ((hydra_connect_to_ssl(sock) == -1)) {
        	if (verbose)
                  hydra_report(stderr, "[ERROR] Can't use TLS\n");
                disable_tls = 1;
                run = 1;
                break;
              } else {
        	if (verbose)
                  hydra_report(stderr, "[VERBOSE] TLS connection done\n");
              }
              /* ask again capability request but in TLS mode */
              if (hydra_send(sock, buffer1, strlen(buffer1), 0) < 0)
        	hydra_child_exit(2);
              buf = smtp_read_server_capacity(sock);
              if (buf == NULL)
        	hydra_child_exit(2);
            }
          }
	}
      }
#endif

      if (buf[0] != '2') {
        if (hydra_send(sock, buffer2, strlen(buffer2), 0) < 0)
          hydra_child_exit(2);

        free(buf);
        buf = smtp_read_server_capacity(sock);

        if (buf == NULL)
          hydra_child_exit(2);
      }

      if ((strstr(buf, "LOGIN") == NULL) && (strstr(buf, "NTLM") != NULL)) {
        smtp_auth_mechanism = AUTH_NTLM;
      }
#ifdef LIBOPENSSLNEW
      if ((strstr(buf, "LOGIN") == NULL) && (strstr(buf, "DIGEST-MD5") != NULL)) {
        smtp_auth_mechanism = AUTH_DIGESTMD5;
      }

      if ((strstr(buf, "LOGIN") == NULL) && (strstr(buf, "CRAM-MD5") != NULL)) {
        smtp_auth_mechanism = AUTH_CRAMMD5;
      }
#endif

      if ((strstr(buf, "LOGIN") == NULL) && (strstr(buf, "PLAIN") != NULL)) {
        smtp_auth_mechanism = AUTH_PLAIN;
      }

      if ((miscptr != NULL) && (strlen(miscptr) > 0)) {
        for (i = 0; i < strlen(miscptr); i++)
          miscptr[i] = (char) toupper((int) miscptr[i]);

        if (strncmp(miscptr, "LOGIN", 5) == 0)
          smtp_auth_mechanism = AUTH_LOGIN;

        if (strncmp(miscptr, "PLAIN", 5) == 0)
          smtp_auth_mechanism = AUTH_PLAIN;

#ifdef LIBOPENSSLNEW
        if (strncmp(miscptr, "CRAM-MD5", 8) == 0)
          smtp_auth_mechanism = AUTH_CRAMMD5;

        if (strncmp(miscptr, "DIGEST-MD5", 10) == 0)
          smtp_auth_mechanism = AUTH_DIGESTMD5;
#endif

        if (strncmp(miscptr, "NTLM", 4) == 0)
          smtp_auth_mechanism = AUTH_NTLM;

      }
      if (verbose) {
        switch (smtp_auth_mechanism) {
        case AUTH_LOGIN:
          hydra_report(stderr, "[VERBOSE] using SMTP LOGIN AUTH mechanism\n");
          break;
        case AUTH_PLAIN:
          hydra_report(stderr, "[VERBOSE] using SMTP PLAIN AUTH mechanism\n");
          break;
#ifdef LIBOPENSSLNEW
        case AUTH_CRAMMD5:
          hydra_report(stderr, "[VERBOSE] using SMTP CRAM-MD5 AUTH mechanism\n");
          break;
        case AUTH_DIGESTMD5:
          hydra_report(stderr, "[VERBOSE] using SMTP DIGEST-MD5 AUTH mechanism\n");
          break;
#endif
        case AUTH_NTLM:
          hydra_report(stderr, "[VERBOSE] using SMTP NTLM AUTH mechanism\n");
          break;
        }
      }
      free(buf);
      next_run = 2;
      break;
    case 2:                    /* run the cracking function */
      next_run = start_smtp(sock, ip, port, options, miscptr, fp);
      break;
    case 3:                    /* clean exit */
      if (sock >= 0) {
        sock = hydra_disconnect(sock);
      }
      hydra_child_exit(0);
      return;
    default:
      hydra_report(stderr, "[ERROR] Caught unknown return code, exiting!\n");
      hydra_child_exit(0);
    }
    run = next_run;
  }
}

int service_smtp_init(char *ip, int sp, unsigned char options, char *miscptr, FILE *fp, int port) {
  // called before the childrens are forked off, so this is the function
  // which should be filled if initial connections and service setup has to be
  // performed once only.
  //
  // fill if needed.
  // 
  // return codes:
  //   0 all OK
  //   -1  error, hydra will exit, so print a good error message here

  return 0;
}
