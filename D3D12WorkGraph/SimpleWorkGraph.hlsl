RWStructuredBuffer<uint> rwResult : register(u0);

struct FirstNodeRecord
{
	uint3	GridSize	: SV_DispatchGrid;
	uint	DispatchIndex;
	uint	Value;
};

struct SecondNodeRecord
{
	uint	BufferIndex;
	uint	Value;
};

struct ThirdNodeRecord
{
	uint	BufferIndex;
	uint	Value;
};

struct ForthNodeRecord
{
	uint	BufferIndex;
	uint	Value;
};

#define FirstNodeThreadX 4
#define ENABLE_THIRD_NODE 1

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16,1,1)]
[NumThreads(FirstNodeThreadX, 1, 1)]
void FirstNode(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID,
	uint dtid : SV_DispatchThreadID,
	DispatchNodeInputRecord<FirstNodeRecord> inputRecord,
	[MaxRecords(FirstNodeThreadX)] NodeOutput<SecondNodeRecord> SecondNode)
{
	FirstNodeRecord inRec = inputRecord.Get();
	uint bindex = FirstNodeThreadX * inRec.GridSize.x * inRec.DispatchIndex + dtid;
	uint outindex = gtid;

	GroupNodeOutputRecords<SecondNodeRecord> outRecs = SecondNode.GetGroupNodeOutputRecords(FirstNodeThreadX);
	outRecs[outindex].BufferIndex = bindex;
	outRecs[outindex].Value = inRec.Value;
	outRecs.OutputComplete();
}

#if ENABLE_THIRD_NODE == 0

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(16,1,1)]
void SecondNode(
	uint gtid : SV_GroupThreadID,
	[MaxRecords(16)] GroupNodeInputRecords<SecondNodeRecord> inputRecord)
{
	if (gtid >= inputRecord.Count())
	{
		return;
	}

	uint value = inputRecord[gtid].Value;
	//uint sum = WaveActiveSum(value);
	uint sum = WavePrefixSum(value) + value;
	rwResult[inputRecord[gtid].BufferIndex] = sum;
}

#else

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(16,1,1)]
void SecondNode(
	uint gtid : SV_GroupThreadID,
	[MaxRecords(16)] GroupNodeInputRecords<SecondNodeRecord> inputRecord,
	[MaxRecords(16)] [NodeArraySize(2)] NodeOutputArray<ThirdNodeRecord> ThirdNode,
	[MaxRecords(1)] NodeOutput<ForthNodeRecord> ForthNode)
{
	if (gtid >= inputRecord.Count())
	{
		return;
	}

	uint value = inputRecord[gtid].Value;
	//uint sum = WaveActiveSum(value);
	uint sum = WavePrefixSum(value) + value;

	uint procIndex = (gtid & 0x01);
	ThreadNodeOutputRecords<ThirdNodeRecord> outThread =
		ThirdNode[procIndex].GetThreadNodeOutputRecords(1);
	outThread.Get().BufferIndex = inputRecord[gtid].BufferIndex;
	outThread.Get().Value = sum;

	outThread.OutputComplete();

	bool bLaunchBroadcast = gtid == 0;
	GroupNodeOutputRecords<ForthNodeRecord> outBroadcast = ForthNode.GetGroupNodeOutputRecords(1);
	if (bLaunchBroadcast)
	{
		outBroadcast.Get().BufferIndex = 0;
		outBroadcast.Get().Value = 1000;
	}
	outBroadcast.OutputComplete();
}

[Shader("node")]
[NodeID("ThirdNode", 0)]
[NodeLaunch("thread")]
void ThirdNode0(ThreadNodeInputRecord<ThirdNodeRecord> inputRecord)
{
	uint value = inputRecord.Get().Value;
	uint sum = WavePrefixSum(value) + value;
	uint ov;
	InterlockedAdd(rwResult[inputRecord.Get().BufferIndex], sum, ov);
}

[Shader("node")]
[NodeID("ThirdNode", 1)]
[NodeLaunch("thread")]
void ThirdNode1(ThreadNodeInputRecord<ThirdNodeRecord> inputRecord)
{
	uint value = inputRecord.Get().Value;
	uint sum = WaveActiveSum(value);
	uint ov;
	InterlockedAdd(rwResult[inputRecord.Get().BufferIndex], sum, ov);
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(2,1,1)]
[NumThreads(16, 1, 1)]
void ForthNode(
	uint dtid : SV_DispatchThreadID,
	DispatchNodeInputRecord<ForthNodeRecord> inputRecord)
{
	ForthNodeRecord inRec = inputRecord.Get();
	uint bindex = inRec.BufferIndex + dtid;

	uint ov;
	InterlockedAdd(rwResult[bindex], inRec.Value, ov);
}

#endif
