#include "Tipsify.h"

using std::sort;

#define max(a,b) ((a) > (b) ? (a) : (b))

#define cf(p, c, v) (((p-c+2*v) > iCacheSize) ? (0) : (p-c))

//structure used to sort clusters
class ClusterSort
{
public:
	float dp; //dot product result of cluster
	int i; //index
};

bool sortfunc(const ClusterSort &a, const ClusterSort &b)
{
	return a.dp < b.dp;
}

inline int min(const int a, const int b)
{
	return a < b ? a : b;
}

//function that implements the vcache optimization
float FanVertLinSort(int *piIndexBufferIn, int *piIndexBufferOut, int iNumFaces, int *piScratch, int iCacheSize, int *piClustersOut, int &iNumClusters)
{
	int i = 0;
	int iNumFaces3 = iNumFaces * 3;
	int sum = 0;
	int lowi = 0;
	int j = 0;
	int next = -1;
	int id;

	iNumClusters = 1;
	if (piClustersOut)
		piClustersOut[0] = 0;

	//set array pointers from scratch buffer
	int *piEmitted = piScratch;
	int *piFanList = piEmitted + iNumFaces;
	int *piTriList = piFanList + iNumFaces3;

	int *piStartList = piTriList + iNumFaces3;
	int *piStartListTail = piStartList;

	int *piRemValence = piStartList + iNumFaces3;

	int *piCachePos = piRemValence + iNumFaces3;

	int *piFanPos = piCachePos + iNumFaces3;


	int iCurCachePos = 1 + iCacheSize; //so that cache position of 0 is out of cache
	int iCurCachePosFan;

	//fill in piFanPos with number of triangles adjacent to each vertex
	//fill in piFanList with all vertex id's that appear in this index buffer
	int nv = 0;
	for (i = 0; i < iNumFaces3; i++)
	{
		register int ind = piIndexBufferIn[i];
		piFanPos[ind]++;
		if (piFanPos[ind] == 1)
			piFanList[nv++] = ind;
	}

	//compute a running sum of the counts in piFanPos and update it accordingly
	//this will allow us to reorder the triangles so that they are clustered
	// based on their vertex ids
	for (i = 0; i < nv; i++)
	{
		int x = piFanPos[piFanList[i]];
		piRemValence[sum] = x;
		sum = (piFanPos[piFanList[i]] += sum);
	}

	//Next we write out a list of triangle ids clustered by starting vertex
	//each triangle appears 3 times in this list with a different starting vertex
	// (e.g., 1 2 3, 2 3 1, 3 1 2 being each of the three possible permutations)
	// this way, after clustering, we have clusters of full fans around each vertex 
	// with each triangle appearing 3 times in the list, once for each adjacent vertex
	//In order to make the code efficient, triangles are uniquely identified by an index 
	// into the original buffer. So, id 7 is the second permutation of the third triangle 
	// in the input array.
	for (i = 0; i < iNumFaces3; i++)
	{
		piTriList[--piFanPos[piIndexBufferIn[i]]] = i;
	}

	i = 0;

	//loop through extracting the triangles for the optimized buffer
	while (lowi < iNumFaces3)
	{
		int bestemitted = -INT_MAX;
		int tribest = -1;

		//set current vertex id
		id = piIndexBufferIn[piTriList[i]];

		next = -1;

		iCurCachePosFan = iCurCachePos;

		//loop through extracting all faces from a vertex fan that were not previously written
		while (i < iNumFaces3 && piIndexBufferIn[piTriList[i]] == id)
		{
			//get triangle id, and starting index of that triangle in original buffer (tri3)
			int tri = piTriList[i] / 3;
			int tri3 = tri * 3;

			if (++piEmitted[tri] == 1)
			{
				int *pin = &piIndexBufferIn[tri3];
				int ord = 0;
				for (int ii = 0; ii < 3; ii++, pin++)
				{
					piIndexBufferOut[j++] = *pin;

					int x = piFanPos[*pin];

					int t = iCurCachePos - piCachePos[x] > iCacheSize;
					if (t)
					{
						piCachePos[x] = iCurCachePos++;
					}

					int v = --piRemValence[x];
					if (v > 0 && *pin != id)
					{
						if (t)
						{
							*(piStartListTail++) = *pin;
						}
						int f = cf(iCurCachePosFan, piCachePos[x], v);
						if (f > bestemitted)
						{
							bestemitted = f;
							next = *pin;
						}
					}
				}
			}

			//increment counters into piTriList
			if (i == lowi)
				lowi++;
			i++;
		}

		//reset its fan position, so that it doesn't get processed twice
		piFanPos[id] = 0;

		if (next == -1)
		{
			int notfound = 1;

			while (piStartListTail > piStartList)
			{
				i = piFanPos[*(--piStartListTail)];
				if (i > 0)
				{
					notfound = 0;
					break;
				}
			}

			if (notfound)
			{
				while (lowi < iNumFaces3 && !piFanPos[piIndexBufferIn[piTriList[lowi]]])
				{
					lowi++;
				}
				i = lowi;
			}

			//overdraw output
			if (piClustersOut && piClustersOut[iNumClusters - 1] != j / 3 && iCurCachePos - piCachePos[i] > iCacheSize * 2)
			{
				piClustersOut[iNumClusters++] = j / 3;
			}
		}
		//if we have a neighboring id to fan around, set it as current
		else
		{
			i = piFanPos[next];
		}
	}

	//clear temp array (only the elements used)
	for (i = 0; i < nv; i++)
	{
		piFanPos[piFanList[i]] = 0;
	}
	memset(piScratch, 0, (iNumFaces * 16) * sizeof(int));

	if (piClustersOut && piClustersOut[iNumClusters - 1] == iNumFaces)
		iNumClusters--;

	return (iCurCachePos - iCacheSize - 1) / (float)iNumFaces;
}

//function that implements the overdraw ordering
void OverdrawOrder(int *piIndexBufferIn,
	int *piIndexBufferOut,
	int iNumFaces,
	float *pfVertexPositionsIn,
	int iNumVertices,
	int *piClustersIn, //should have piClustersIn[iNumClusters] == iNumFaces
	int iNumClusters,
	int *piScratch,
	int *piRemap = NULL)
{
	int i, j;
	int c = 0, cstart = 0;
	int cnext = piClustersIn[1];
	int *p = piIndexBufferIn;
	Vector *pvVertexPositionsIn = (Vector *)pfVertexPositionsIn;
	Vector vMeshPositions = Vector(0, 0, 0);
	float fMArea = 0.f;

	int *piScratchBase = piScratch;
	Vector *pvClusterPositions = (Vector *)piScratch;
	piScratch += iNumClusters * 3;

	Vector *pvClusterNormals = (Vector *)piScratch;
	piScratch += iNumClusters * 3;

	ClusterSort *cs = (ClusterSort *)piScratch;
	piScratch += iNumClusters * 2;

	for (i = 0; i < iNumClusters; i++)
	{
		pvClusterPositions[i] = Vector(0, 0, 0);
		pvClusterNormals[i] = Vector(0, 0, 0);
	}
	float fCArea = 0.f;
	for (i = 0; i <= iNumFaces; i++)
	{
		if (i == cnext)
		{
			pvClusterPositions[c] /= fCArea * 3.f;
			pvClusterNormals[c].normalize();
			c++;
			if (c == iNumClusters)
				break;
			cstart = i;
			cnext = piClustersIn[c + 1];
			fCArea = 0.f;
		}

		Vector vNormal = cross(pvVertexPositionsIn[p[2]] - pvVertexPositionsIn[p[0]],
			pvVertexPositionsIn[p[1]] - pvVertexPositionsIn[p[0]]);
		float fArea = vNormal.length();
		if (fArea > 0.f)
		{
			vNormal /= fArea;
		}
		else
		{
			fArea = 0.f;
			vNormal = Vector(0, 0, 0);
		}

		for (j = 0; j < 3; j++)
		{
			Vector *vp = (Vector *)&pfVertexPositionsIn[(*p) * 3];
			vMeshPositions += *vp * fArea;
			pvClusterPositions[c] += *vp * fArea;
			p++;
		}
		pvClusterNormals[c] += vNormal;

		fMArea += fArea;
		fCArea += fArea;
	}
	vMeshPositions /= fMArea * 3.f;

	for (i = 0; i < iNumClusters; i++)
	{
		cs[i].dp = dot(pvClusterPositions[i] - vMeshPositions, pvClusterNormals[i]);
		if (cs[i].dp < -2e20 || cs[i].dp > 2e20)
		{
			cs[i].dp = 0.f;
		}
		cs[i].i = i;
	}

	std::sort(cs, cs + iNumClusters, sortfunc);

	int jj = 0;
	for (i = 0; i < iNumClusters; i++)
	{
		for (j = piClustersIn[cs[i].i] * 3; j < piClustersIn[cs[i].i + 1] * 3; j++)
			piIndexBufferOut[jj++] = piIndexBufferIn[j];
	}

	if (piRemap != NULL)
	{
		for (i = 0; i < iNumClusters; i++)
		{
			piRemap[i] = cs[i].i;
		}
	}

	memset(piScratchBase, 0, (piScratch - piScratchBase) * sizeof(int));
}

//overdraw order based on integral
void OverdrawOrderIntegral(int *piIndexBufferIn,
	int *piIndexBufferOut,
	int iNumFaces,
	float *pfVertexPositionsIn,
	int iNumVertices,
	int *piClustersIn, //should have piClustersIn[iNumClusters] == iNumFaces
	int iNumClusters,
	int *piScratch,
	int *piRemap = NULL)
{
	int i, j;
	int c = 0, cstart = 0;
	int cnext = piClustersIn[1];
	int *p = piIndexBufferIn;
	Vector *pvVertexPositionsIn = (Vector *)pfVertexPositionsIn;
	Vector vMeshPositions = Vector(0, 0, 0);
	float fMArea = 0.f;

	int *piScratchBase = piScratch;
	Vector *pvClusterPositions = (Vector *)piScratch;
	piScratch += iNumClusters * 3;

	Vector *pvClusterNormals = (Vector *)piScratch;
	piScratch += iNumClusters * 3;

	float *pfClusterAreas = (float *)piScratch;
	piScratch += iNumClusters;

	ClusterSort *cs = (ClusterSort *)piScratch;
	piScratch += iNumClusters * 2;

	for (i = 0; i < iNumClusters; i++)
	{
		pvClusterPositions[i] = Vector(0, 0, 0);
		pvClusterNormals[i] = Vector(0, 0, 0);
	}
	float fCArea = 0.f;
	for (i = 0; i <= iNumFaces; i++)
	{
		if (i == cnext)
		{
			pfClusterAreas[c] = fCArea;
			pvClusterPositions[c] /= fCArea * 3.f;
			pvClusterNormals[c].normalize();
			c++;
			if (c == iNumClusters)
				break;
			cstart = i;
			cnext = piClustersIn[c + 1];
			fCArea = 0.f;
		}

		Vector vNormal = cross(pvVertexPositionsIn[p[2]] - pvVertexPositionsIn[p[0]],
			pvVertexPositionsIn[p[1]] - pvVertexPositionsIn[p[0]]);
		float fArea = vNormal.length();
		if (fArea > 0.f)
		{
			vNormal /= fArea;
		}
		else
		{
			fArea = 0.f;
			vNormal = Vector(0, 0, 0);
		}

		for (j = 0; j < 3; j++)
		{
			Vector *vp = (Vector *)&pfVertexPositionsIn[(*p) * 3];
			vMeshPositions += *vp * fArea;
			pvClusterPositions[c] += *vp * fArea;
			p++;
		}

		pvClusterNormals[c] += vNormal;

		fMArea += fArea;
		fCArea += fArea;
	}
	vMeshPositions /= fMArea * 3.f;

	for (i = 0; i < iNumClusters; i++)
	{
		cs[i].dp = 0.f;
		for (j = 0; j < iNumClusters; j++)
		{
			if (i == j)
				continue;
			Vector vec = pvClusterPositions[i] - pvClusterPositions[j];
			vec.normalize();
			float da = dot(vec, pvClusterNormals[j]);
			float db = dot(vec, pvClusterNormals[i]);
			float dn = dot(pvClusterNormals[i], pvClusterNormals[j]);
			if (da > 0 && db > 0)
			{
				cs[i].dp += da * db * pfClusterAreas[j];
			}
			//if(da > 0 && db > 0 && dn > 0)
			//   cs[i].dp += dn * pfClusterAreas[j];
		}
		//cs[i].dp /= fMArea - pfClusterAreas[i];
		cs[i].i = i;
	}

	std::sort(cs, cs + iNumClusters, sortfunc);

	int jj = 0;
	for (i = 0; i < iNumClusters; i++)
	{
		for (j = piClustersIn[cs[i].i] * 3; j < piClustersIn[cs[i].i + 1] * 3; j++)
			piIndexBufferOut[jj++] = piIndexBufferIn[j];
	}

	if (piRemap != NULL)
	{
		for (i = 0; i < iNumClusters; i++)
		{
			piRemap[i] = cs[i].i;
		}
	}

	memset(piScratchBase, 0, (piScratch - piScratchBase) * sizeof(int));
}

//function implements linear clustering
int OverdrawOrderPartition(int *piIndexBufferIn,
	int iNumFaces,
	int *piClustersIn, //should have piClustersIn[iNumClusters] == iNumFaces
	int iNumClustersIn,
	int iCacheSize,
	float lambda,
	int *piClustersOut,
	int *piScratch)
{

	int *piScratchBase = piScratch;
	int *piCache = piScratch;
	piScratch += iCacheSize;

	int i;
	int j = 0;

	for (i = 0; i < iNumClustersIn; i++)
	{
		piClustersOut[j++] = piClustersIn[i];
		int *p = piIndexBufferIn;
		int n = piClustersIn[i + 1] - piClustersIn[i];
		int start = piClustersIn[i];
		int head = 0;
		int m, k;
		int iProc = 0;
		for (m = 0; m < iCacheSize; m++)
		{
			piCache[m] = -1;
		}
		for (k = 0; k < n; k++, p += 3)
		{
			for (m = 0; m < 3; m++)
			{
				int found = 0;
				for (int q = 0; q < iCacheSize; q++)
				{
					if (p[m] == piCache[q])
					{
						found = 1;
						break;
					}
				}

				if (!found)
				{
					iProc++;
					piCache[head] = p[m];
					head = (head + 1);
					if (head == iCacheSize)
						head = 0;
				}
			}

			float fEstProc = iProc / (float)(k + 1);
			if (k > 0 && lambda > fEstProc)
			{
				start += k;
				piClustersOut[j++] = start;
				n -= k;
				k = -1;
				p -= 3;
				iProc = 0;
				for (m = 0; m < iCacheSize; m++)
				{
					piCache[m] = -1;
				}
			}
		}
	}
	piClustersOut[j] = iNumFaces;

	memset(piScratchBase, 0, (piScratch - piScratchBase) * sizeof(int));

	return j;
}

//function that computes size of scratch memory
int FanVertScratchSize(int iNumVertices, int iNumFaces)
{
	return (iNumFaces * 22 + iNumVertices + 3) * sizeof(int);
}

//main optimization function
void FanVertOptimize(float *pfVertexPositionsIn,   //vertex buffer positions, 3 floats per vertex
	int *piIndexBufferIn,         //index buffer positions, 3 ints per vertex
	int *piIndexBufferOut,        //updated index buffer (the output of the algorithm)
	int iNumVertices,             //# of vertices in the vertex buffer
	int iNumFaces,                //# of faces in the index buffer
	int iCacheSize,               //hardware cache size

	float alpha,                  //constant parameter to compute lambda term from algorithm 
	float beta,                   //linear parameter to compute lambda term from algorithm 
	//lambda = alpha + beta * ACMR_OF_TIPSY

	int *piScratch,        //optional temp buffer for computations; its size in bytes should be:
	//FanVertScratchSize(iNumVertices, iNumFaces)
	//if NULL is passed, function will allocate and free this data

	int *piClustersOut ,    //optional buffer for the output cluster position (in faces) of each cluster
	int *piNumClustersOut) //the number of putput clusters
{
	bool bMalloc = false;
	if (piScratch == NULL)
	{
		int iScratchSize = FanVertScratchSize(iNumVertices, iNumFaces);
		piScratch = (int *)malloc(iScratchSize);
		memset(piScratch, 0, iScratchSize);
		bMalloc = true;
	}
	int *piScratchBase = piScratch;

	int *piIndexBufferTmp = piScratch;
	piScratch += iNumFaces * 3;

	int *piClustersIn = piScratch;
	piScratch += iNumFaces + 1;

	int *piClustersTmp = piScratch;
	piScratch += iNumFaces + 1;

	int *piClusterRemap = piScratch;
	piScratch += iNumFaces + 1;


	int iNumClusters;
	float lambda = FanVertLinSort(piIndexBufferIn, piIndexBufferTmp, iNumFaces,
		piScratch, iCacheSize, piClustersIn, iNumClusters);

	lambda = alpha + beta * lambda;

	int iNumClustersOut = OverdrawOrderPartition(piIndexBufferTmp, iNumFaces,
		piClustersIn, iNumClusters, iCacheSize, lambda, piClustersTmp, piScratch);

	OverdrawOrder(piIndexBufferTmp, piIndexBufferOut,
		iNumFaces, pfVertexPositionsIn,
		iNumVertices,
		piClustersTmp,
		iNumClustersOut,
		piScratch,
		piClusterRemap);

	if (piNumClustersOut != NULL)
	{
		*piNumClustersOut = iNumClustersOut;
		int i;

		// convert ClustersTmp array to array of cluster sizes
		for (i = 1; i <= iNumClustersOut; i++)
		{
			piClustersTmp[i - 1] = piClustersTmp[i] - piClustersTmp[i - 1];
		}

		// copy it to piClustersOut, applying remap
		for (i = 0; i < iNumClustersOut; i++)
		{
			piClustersOut[i] = piClustersTmp[piClusterRemap[i]];
		}

		// convert from cluster sizes back to cluster offsets
		int nOffset = 0;
		for (i = 0; i <= iNumClustersOut; i++)
		{
			int nSizeThisCluster = piClustersOut[i];
			piClustersOut[i] = nOffset;
			nOffset += nSizeThisCluster;
		}

		if (piClustersOut[iNumClustersOut] != iNumFaces)
		{
			printf("DOH\n");
		}
	}

	if (piScratch - piScratchBase > 0)
		memset(piScratchBase, 0, (piScratch - piScratchBase) * sizeof(int)); //clear memory from tmp
	if (bMalloc)
	{
		free(piScratchBase);
	}

}

//function that only performs vertex cache optimization part of the algorithm
void FanVertOptimizeVCacheOnly(int *piIndexBufferIn,
	int *piIndexBufferOut,
	int iNumVertices,
	int iNumFaces,
	int iCacheSize,
	int *piScratch ,
	int *piClustersOut,
	int *iNumClusters)
{
	bool bMalloc = false;
	if (piScratch == NULL)
	{
		int iScratchSize = FanVertScratchSize(iNumVertices, iNumFaces);
		piScratch = (int *)malloc(iScratchSize);
		memset(piScratch, 0, iScratchSize);
		bMalloc = true;
	}
	int *piScratchBase = piScratch;

	if (piClustersOut == NULL)
	{
		piClustersOut = piScratch;
		piScratch += iNumFaces + 1;
	}

	int nc;
	float lambda = FanVertLinSort(piIndexBufferIn, piIndexBufferOut, iNumFaces,
		piScratch, iCacheSize, piClustersOut, nc);
	if (iNumClusters)
		*iNumClusters = nc;

	if (piScratch - piScratchBase > 0)
		memset(piScratchBase, 0, (piScratch - piScratchBase) * sizeof(int)); //clear memory from tmp
	if (bMalloc)
	{
		free(piScratchBase);
	}
}

//function that only performs the clustering step
void FanVertOptimizeClusterOnly(int *piIndexBufferIn,
	int iNumVertices,
	int iNumFaces,
	int iCacheSize,
	float lambda,
	int *piClustersIn,
	int iNumClusters,
	int *piClustersOut,
	int *iNumClustersOut,
	int *piScratch )
{
	bool bMalloc = false;
	if (piScratch == NULL)
	{
		int iScratchSize = FanVertScratchSize(iNumVertices, iNumFaces);
		piScratch = (int *)malloc(iScratchSize);
		memset(piScratch, 0, iScratchSize);
		bMalloc = true;
	}

	*iNumClustersOut = OverdrawOrderPartition(piIndexBufferIn, iNumFaces,
		piClustersIn, iNumClusters, iCacheSize, lambda, piClustersOut, piScratch);

	if (bMalloc)
	{
		free(piScratch);
	}
}

//function that only performs the overdraw step
void FanVertOptimizeOverdrawOnly(float *pfVertexPositionsIn,
	int *piIndexBufferIn,
	int *piIndexBufferOut,
	int iNumVertices,
	int iNumFaces,
	int iCacheSize,
	float lambda,
	int *piClustersIn,
	int iNumClusters,
	int *piScratch ,
	int *piRemap)
{
	bool bMalloc = false;
	if (piScratch == NULL)
	{
		int iScratchSize = FanVertScratchSize(iNumVertices, iNumFaces);
		piScratch = (int *)malloc(iScratchSize);
		memset(piScratch, 0, iScratchSize);
		bMalloc = true;
	}

	int *piScratchBase = piScratch;

	OverdrawOrder(piIndexBufferIn, piIndexBufferOut,
		iNumFaces, pfVertexPositionsIn,
		iNumVertices,
		piClustersIn,
		iNumClusters,
		piScratch,
		piRemap);

	if (bMalloc)
	{
		free(piScratchBase);
	}
}

