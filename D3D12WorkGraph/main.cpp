#include <windows.h>
#include <atlbase.h>
#include <memory>
#include <vector>
#include "dxcapi.h"
#include "d3d12.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 711; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

struct ShaderCompiler
{
	HMODULE	hDllModule = 0;
	CComPtr<IDxcLibrary> Library = nullptr;
	CComPtr<IDxcCompiler> Compiler = nullptr;
	CComPtr<IDxcIncludeHandler> IncludeHandler = nullptr;

	~ShaderCompiler()
	{
		Library.Release();
		Compiler.Release();
		IncludeHandler.Release();
	}
};

struct D3DContext
{
	CComPtr<ID3D12DeviceExperimental> Device;
	CComPtr<ID3D12GraphicsCommandListExperimental> CommandList;
	CComPtr<ID3D12CommandQueue> CommandQueue;
	CComPtr<ID3D12CommandAllocator> CommandAllocator;
	CComPtr<ID3D12Fence> Fence;
	UINT64 FenceValue;
	HANDLE hEvent;

	~D3DContext()
	{
		Fence.Release();
		CommandList.Release();
		CommandAllocator.Release();
		CommandQueue.Release();
		Device.Release();
		CloseHandle(hEvent);
	}
};

bool CreateShaderCompiler(std::unique_ptr<ShaderCompiler>& Compiler)
{
	Compiler = std::make_unique<ShaderCompiler>();

	Compiler->hDllModule = LoadLibrary(L"dxcompiler.dll");
	if (!Compiler->hDllModule)
	{
		return false;
	}

	DxcCreateInstanceProc CreateProc = (DxcCreateInstanceProc)GetProcAddress(Compiler->hDllModule, "DxcCreateInstance");
	if (!CreateProc)
	{
		return false;
	}

	HRESULT hr = CreateProc(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<LPVOID*>(&Compiler->Library));
	if (FAILED(hr))
	{
		return false;
	}

	hr = Compiler->Library->CreateIncludeHandler(&Compiler->IncludeHandler);
	if (FAILED(hr))
	{
		return false;
	}

	hr = CreateProc(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<LPVOID*>(&Compiler->Compiler));
	if (FAILED(hr))
	{
		return false;
	}

	return true;
}

bool CompilerShaderFromFile(ShaderCompiler* Compiler, LPCWSTR File, LPCWSTR Profile, ID3DBlob** ppCodeBin)
{
	CComPtr<IDxcBlobEncoding> Source;
	HRESULT hr = Compiler->Library->CreateBlobFromFile(File, nullptr, &Source);
	if (FAILED(hr))
	{
		return false;
	}

	CComPtr<IDxcOperationResult> OpResult;
	hr = Compiler->Compiler->Compile(
		Source,
		nullptr,
		nullptr,
		Profile,
		nullptr, 0,
		nullptr, 0,
		Compiler->IncludeHandler,
		&OpResult);
	if (FAILED(hr))
	{
		return false;
	}

	OpResult->GetStatus(&hr);
	if (SUCCEEDED(hr))
	{
		hr = OpResult->GetResult((IDxcBlob**)ppCodeBin);
	}
	else
	{
		CComPtr<IDxcBlobEncoding> Errors;
		if (SUCCEEDED(OpResult->GetErrorBuffer(&Errors)))
		{
			fprintf_s(stderr, "Shader Compile Error: %s\n", (LPCSTR)Errors->GetBufferPointer());
			return false;
		}
	}

	return true;
}

bool CreateD3DContext(std::unique_ptr<D3DContext>& Context)
{
	UUID Features[2] = { D3D12ExperimentalShaderModels, D3D12StateObjectsExperiment };
	HRESULT hr = D3D12EnableExperimentalFeatures(ARRAYSIZE(Features), Features, nullptr, nullptr);
	if (FAILED(hr))
	{
		fprintf_s(stderr, "Non development mode.\n");
		return false;
	}

	Context = std::make_unique<D3DContext>();

	Context->hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	CComPtr<ID3D12Debug1> pDebug;
	hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug));
	if (FAILED(hr))
	{
		return false;
	}
	pDebug->EnableDebugLayer();

	D3D_FEATURE_LEVEL FL = D3D_FEATURE_LEVEL_11_0;
	hr = D3D12CreateDevice(NULL, FL, IID_PPV_ARGS(&Context->Device));
	if (FAILED(hr))
	{
		return false;
	}

	hr = Context->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Context->Fence));
	if (FAILED(hr))
	{
		return false;
	}

	hr = Context->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Context->CommandAllocator));
	if (FAILED(hr))
	{
		return false;
	}

	D3D12_COMMAND_QUEUE_DESC cqDesc = {};
	cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = Context->Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&Context->CommandQueue));
	if (FAILED(hr))
	{
		return false;
	}

	hr = Context->Device->CreateCommandList(0, 	D3D12_COMMAND_LIST_TYPE_DIRECT, Context->CommandAllocator, nullptr, IID_PPV_ARGS(&Context->CommandList));
	if (FAILED(hr))
	{
		return false;
	}

	return true;
}

void FlushCommand(D3DContext* Context)
{
	Context->CommandList->Close();
	ID3D12CommandList* pCmdList = Context->CommandList;
	Context->CommandQueue->ExecuteCommandLists(1, &pCmdList);

	Context->CommandQueue->Signal(Context->Fence, ++Context->FenceValue);
	Context->Fence->SetEventOnCompletion(Context->FenceValue, Context->hEvent);

	DWORD waitResult = WaitForSingleObject(Context->hEvent, INFINITE);
	if (waitResult != WAIT_OBJECT_0)
	{
		throw E_FAIL;
	}
	Context->Device->GetDeviceRemovedReason();

	Context->CommandAllocator->Reset();
	Context->CommandList->Reset(Context->CommandAllocator, nullptr);
}


int main()
{
	// create shader compiler.
	std::unique_ptr<ShaderCompiler> Compiler;
	if (!CreateShaderCompiler(Compiler))
	{
		return -1;
	}

	// create d3d context.
	std::unique_ptr<D3DContext> Context;
	if (!CreateD3DContext(Context))
	{
		return -1;
	}

	// compile shader.
	CComPtr<ID3DBlob> ShaderCodeBin;
	if (!CompilerShaderFromFile(Compiler.get(), L"SimpleWorkGraph.hlsl", L"lib_6_8", &ShaderCodeBin))
	{
		return -1;
	}

	// create state object.
	static LPCWSTR kProgramName = L"SimpleWorkGraph";
	CComPtr<ID3D12RootSignature> RootSig;
	CComPtr<ID3D12StateObject> StateObj;
	CComPtr<ID3D12Resource> BackingMemory;
	D3D12_PROGRAM_IDENTIFIER hWorkGraph;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE BackingMemRange;
	{
		D3D12_ROOT_PARAMETER1 param{};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		param.Descriptor.ShaderRegister = 0;
		param.Descriptor.RegisterSpace = 0;
		param.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
		param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
		desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
		desc.Desc_1_2.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		desc.Desc_1_2.pParameters = &param;
		desc.Desc_1_2.NumParameters = 1;
		desc.Desc_1_2.pStaticSamplers = nullptr;
		desc.Desc_1_2.NumStaticSamplers = 0;

		CComPtr<ID3DBlob> blob;
		CComPtr<ID3DBlob> error;
		bool ret = true;

		HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &error);
		if (FAILED(hr))
		{
			return -1;
		}

		hr = Context->Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&RootSig));
		if (FAILED(hr))
		{
			return -1;
		}
	}
	{
		D3D12_DXIL_LIBRARY_DESC libDesc{};
		libDesc.DXILLibrary.pShaderBytecode = ShaderCodeBin->GetBufferPointer();
		libDesc.DXILLibrary.BytecodeLength = ShaderCodeBin->GetBufferSize();

		D3D12_WORK_GRAPH_DESC wgDesc{};
		wgDesc.ProgramName =kProgramName;
		wgDesc.Flags = D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES;

		D3D12_STATE_SUBOBJECT subObjects[3]{};
		subObjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		subObjects[0].pDesc = &libDesc;
		subObjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH;
		subObjects[1].pDesc = &wgDesc;
		subObjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		subObjects[2].pDesc = &RootSig.p;

		D3D12_STATE_OBJECT_DESC desc{};
		desc.pSubobjects = subObjects;
		desc.NumSubobjects = ARRAYSIZE(subObjects);
		desc.Type = D3D12_STATE_OBJECT_TYPE_EXECUTABLE;

		HRESULT hr = Context->Device->CreateStateObject(&desc, IID_PPV_ARGS(&StateObj));
		if (FAILED(hr))
		{
			return -1;
		}
	}
	{
		CComPtr<ID3D12StateObjectProperties1> SOProps;
		SOProps = StateObj;
		hWorkGraph = SOProps->GetProgramIdentifier(kProgramName);

		CComPtr<ID3D12WorkGraphProperties> WGProps;
		WGProps = StateObj;

		UINT WorkGraphIndex = WGProps->GetWorkGraphIndex(kProgramName);
		D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS MemReqs;
		WGProps->GetWorkGraphMemoryRequirements(WorkGraphIndex, &MemReqs);

		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = MemReqs.MaxSizeInBytes;
		desc.Height = desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap.CreationNodeMask = 1;
		heap.VisibleNodeMask = 1;

		HRESULT hr = Context->Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&BackingMemory));
		if (FAILED(hr))
		{
			return -1;
		}

		BackingMemRange.SizeInBytes = desc.Width;
		BackingMemRange.StartAddress = BackingMemory->GetGPUVirtualAddress();
	}

	// create buffers.
	static const UINT kDataCount = 65536;
	CComPtr<ID3D12Resource> ResultBuffer;
	CComPtr<ID3D12Resource> ReadBackBuffer;
	{
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = kDataCount * sizeof(UINT);
		desc.Height = desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap.CreationNodeMask = 1;
		heap.VisibleNodeMask = 1;

		HRESULT hr = Context->Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&ResultBuffer));
		if (FAILED(hr))
		{
			return -1;
		}

		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		heap.Type = D3D12_HEAP_TYPE_READBACK;

		hr = Context->Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&ReadBackBuffer));
		if (FAILED(hr))
		{
			return -1;
		}
	}

	// execute work graph.
	{
		// ready input record.
		struct FirstNodeRecord
		{
			UINT	GridSize[3];
			UINT	DispatchIndex;
			UINT	Value;
		};
		FirstNodeRecord InputRecords[] = {
			{{2, 1, 1}, 0, 1},
			{{2, 1, 1}, 1, 3},
		};

		// setup program.
		Context->CommandList->SetComputeRootSignature(RootSig);
		Context->CommandList->SetComputeRootUnorderedAccessView(0, ResultBuffer->GetGPUVirtualAddress());

		D3D12_SET_PROGRAM_DESC SetProgram = {};
		SetProgram.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
		SetProgram.WorkGraph.ProgramIdentifier = hWorkGraph;
		SetProgram.WorkGraph.Flags = D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
		SetProgram.WorkGraph.BackingMemory = BackingMemRange;
		Context->CommandList->SetProgram(&SetProgram);

		// dispatch graph.
		D3D12_DISPATCH_GRAPH_DESC DSDesc = {};
		DSDesc.Mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
		DSDesc.NodeCPUInput.EntrypointIndex = 0;
		DSDesc.NodeCPUInput.NumRecords = ARRAYSIZE(InputRecords);
		DSDesc.NodeCPUInput.RecordStrideInBytes = sizeof(FirstNodeRecord);
		DSDesc.NodeCPUInput.pRecords = InputRecords;
		Context->CommandList->DispatchGraph(&DSDesc);

		FlushCommand(Context.get());
	}

	// readback result.
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = ResultBuffer;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		Context->CommandList->ResourceBarrier(1, &barrier);

		Context->CommandList->CopyResource(ReadBackBuffer, ResultBuffer);

		FlushCommand(Context.get());

		void* data;
		ReadBackBuffer->Map(0, nullptr, &data);
		std::vector<UINT> result;
		result.resize(kDataCount);
		memcpy(result.data(), data, kDataCount * sizeof(UINT));
		ReadBackBuffer->Unmap(0, nullptr);

		for (UINT i = 0; i < 16; i++)
		{
			fprintf_s(stdout, "%d, %d, %d, %d\n", result[i * 4 + 0], result[i * 4 + 1], result[i * 4 + 2], result[i * 4 + 3]);
		}
	}

	return 0;
}
