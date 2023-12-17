RWStructuredBuffer<uint> rwResult : register(u0);

struct FirstNodeRecord
{
	uint3	GridSize	: SV_DispatchGrid;
	uint	Value;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16,1,1)]
[NumThreads(4, 1, 1)]
void FirstNode(
	DispatchNodeInputRecord<FirstNodeRecord> inputRecord,
	uint3 dtid : SV_DispatchThreadID)
{
	uint index = dtid.x;
	uint ov;
	//InterlockedAdd(rwResult[index], index * inputRecord.Get().Value, ov);
	rwResult[index] = index * inputRecord.Get().Value;
}
