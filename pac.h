int pac_init(char *js, void (*notify_cb)(void *), void *arg);
int pac_find_proxy(char *url, char *host,
                   void (*cb)(char *_result, void *_arg), void *arg);
void pac_run_callbacks(void);
