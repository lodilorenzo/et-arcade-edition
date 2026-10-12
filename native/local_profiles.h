/* Local player profiles (native/local_profiles.c, game module). */
#ifndef LOCAL_PROFILES_H
#define LOCAL_PROFILES_H

typedef struct {
    char name[36];
    float skillpoints[SK_NUM_SKILLS];
    int medals[SK_NUM_SKILLS];
    int matches;
} localProfile_t;

void G_LocalProfileInit(void);
void G_LocalProfileConnect(int clientNum);
void G_LocalProfileName(int clientNum, char *netname, int size);
void G_LocalProfileDisconnect(int clientNum);
void G_LocalProfileSaveAll(qboolean finishedMatch);
void G_LocalProfileFrame(void);
void G_LocalProfileTest(void);
qboolean G_LocalProfileId(const char *id);
qboolean G_LocalProfileRead(const char *id, localProfile_t *profile);

#endif
