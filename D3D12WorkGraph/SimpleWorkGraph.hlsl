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

#define FirstNodeThreadX 4
#define ENABLE_THIRD_NODE 0

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
	[MaxRecords(16)] NodeOutput<ThirdNodeRecord> ThirdNode)
{
	if (gtid >= inputRecord.Count())
	{
		return;
	}
	
	uint value = inputRecord[gtid].Value;
	//uint sum = WaveActiveSum(value);
	uint sum = WavePrefixSum(value) + value;

	bool bThreadLaunch = (sum & 0x01) != 0;
	ThreadNodeOutputRecords<ThirdNodeRecord> outRecs =
		ThirdNode.GetThreadNodeOutputRecords(bThreadLaunch ? 1 : 0);
	if (bThreadLaunch)
	{
		outRecs.Get().BufferIndex = inputRecord[gtid].BufferIndex;
		outRecs.Get().Value = sum;
	}
	outRecs.OutputComplete();
}

[Shader("node")]
[NodeLaunch("thread")]
void ThirdNode(ThreadNodeInputRecord<ThirdNodeRecord> inputRecord)
{
	uint value = inputRecord.Get().Value;
	uint sum = WavePrefixSum(value) + value;
	rwResult[inputRecord.Get().BufferIndex] = sum;
}

#endif
