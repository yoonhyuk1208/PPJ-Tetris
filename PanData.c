
// 22x12 �迭 ����
// �迭��� 2 : ���
// �迭��� 1 : ����
// �迭��� 0 : ����


void PanMap(int(*arr)[12]) {
	for (int i = 0; i < 12; i++) {
		arr[0][i] = 2;
		arr[21][i] = 2;
	}

	for (int i = 0; i < 22; i++) {
		arr[i][0] = 2;
		arr[i][11] = 2;
	}

	for(int i = 1; i < 21; i++) {
		for (int j = 1; j < 11; j++) {
			arr[i][j] = 0;
		}
	}
}