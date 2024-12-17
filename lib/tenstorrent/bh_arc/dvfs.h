#ifndef DVFS_H
#define DVFS_H

extern bool dvfs_enabled;

void InitDVFS(void);
void StartDVFSTimer(void);
void DVFSChange(void);

#endif