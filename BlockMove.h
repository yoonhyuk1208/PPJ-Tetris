#pragma once

void LeftMove(int(*arr)[12]);
void RightMove(int(*arr)[12]);
int  Rotate(int(*arr)[12], int nType, int nextRot);
int  SoftDropOne(int(*arr)[12], int curType);  // returns 1 if locked
void HardDrop(int(*arr)[12], int curType);
