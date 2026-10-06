// Reuse the ABI fixture's device, image and command-list helpers.
#include "host_fixture.hpp"
#include "MochizukiProfile.h"
#include <algorithm>
#include <array>

using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}
struct TimingSlot {
    List in, out;
    LmxxfNrJob job{sizeof job};
    uint64_t serial{}, tail{};
    unsigned mode{}, block{};
    double prepare{}, record{}, enqueue{}, submit{}, wait{};
    explicit TimingSlot(ID3D12Device* device): in(device),out(device) {}
};
int wmain(int argc, wchar_t** argv) try {
    Require(argc >= 11, "DLL assets csv depth detailed frames warmup width height dump [photo.rgba] [blocks] [--same-path] required");
    const unsigned depth=unsigned(_wtoi(argv[4])), detailed=unsigned(_wtoi(argv[5]));
    const unsigned frames=unsigned(_wtoi(argv[6])), warmup=unsigned(_wtoi(argv[7]));
    const unsigned width=unsigned(_wtoi(argv[8])), height=unsigned(_wtoi(argv[9]));
    const unsigned blocks=argc>12?unsigned(_wtoi(argv[12])):1;
    const bool samePath=argc>13 && std::wstring(argv[13])==L"--same-path";
    Require(depth>=1 && depth<=3 && detailed<=1 && frames>0 && warmup>0 && width && height,"invalid options");
    HMODULE dll=LoadLibraryW(argv[1]); Require(dll!=nullptr,"load runtime");
    auto get=reinterpret_cast<PFN_MochizukiNrGetApi>(GetProcAddress(dll,"MochizukiNrGetApi"));
    auto defaults=reinterpret_cast<PFN_MochizukiNrGetControlDefaults>(GetProcAddress(dll,"MochizukiNrGetControlDefaults"));
    auto set=reinterpret_cast<PFN_MochizukiNrSetControls>(GetProcAddress(dll,"MochizukiNrSetControls"));
    auto prepare=reinterpret_cast<PFN_MochizukiNrPrepareFrame>(GetProcAddress(dll,"MochizukiNrPrepareFrame"));
    auto info=reinterpret_cast<PFN_MochizukiNrGetInfo>(GetProcAddress(dll,"MochizukiNrGetInfo"));
    auto progress=reinterpret_cast<PFN_MochizukiNrGetBuildProgress>(GetProcAddress(dll,"MochizukiNrGetBuildProgress"));
    auto enable=reinterpret_cast<PFN_MochizukiProfileEnable>(GetProcAddress(dll,"MochizukiProfileEnable"));
    auto read=reinterpret_cast<PFN_MochizukiProfileRead>(GetProcAddress(dll,"MochizukiProfileRead"));
    auto inputMode=reinterpret_cast<PFN_MochizukiProfileInputMode>(GetProcAddress(dll,"MochizukiProfileInputMode"));
    Require(blocks>=1 && (blocks==1 || inputMode),"input-path diagnostic required for blocks");
    Require(!samePath || blocks>1,"--same-path requires multiple diagnostic blocks");
    Require(get&&defaults&&set&&prepare&&info&&progress,"required exports");
    Require(!detailed || (enable&&read),"diagnostic build required for detailed mode");
    api.struct_size=sizeof api; Rc(get(LMXXF_NR_ABI_VERSION,&api));
    ComPtr<IDXGIFactory6> factory; Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter; ComPtr<ID3D12Device> device;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)==S_OK;i++,adapter.Reset()) {
        DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc);
        if(desc.VendorId==0x1002 && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) &&
           SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))) {
            printf("GPU vendor=%04x device=%04x memory=%llu\n",desc.VendorId,desc.DeviceId,(unsigned long long)desc.DedicatedVideoMemory);break;
        }
    }
    Require(device!=nullptr,"AMD adapter required");
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    UINT64 frequency{}; Hr(queue->GetTimestampFrequency(&frequency)); Require(frequency>0,"D3D timestamp frequency");
    ComPtr<ID3D12Fence> fence; Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    uint64_t fenceValue=0;
    ComPtr<ID3D12QueryHeap> heap; D3D12_QUERY_HEAP_DESC hd{}; hd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; hd.Count=depth*4;
    Hr(device->CreateQueryHeap(&hd,IID_PPV_ARGS(&heap)));
    auto queryReadback=Buffer(device.Get(),depth*4*sizeof(uint64_t),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Frame frame(device.Get(),width,height,DXGI_FORMAT_R11G11B10_FLOAT);
    if(argc>11 && argv[11][0]) {
        FILE* file=nullptr; Require(_wfopen_s(&file,argv[11],L"rb")==0,"photo input open");
        std::vector<float> pixels(size_t(width)*height*4);
        Require(fread(pixels.data(),sizeof(float),pixels.size(),file)==pixels.size(),"photo input dimensions");
        Require(fgetc(file)==EOF,"photo input has trailing bytes"); fclose(file);
        void* mapped=nullptr; Hr(frame.upload->Map(0,nullptr,&mapped));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            auto* row=reinterpret_cast<DirectX::PackedVector::XMFLOAT3PK*>(static_cast<char*>(mapped)+y*frame.footprint.Footprint.RowPitch);
            const auto* p=&pixels[(size_t(y)*width+x)*4]; row[x]=DirectX::PackedVector::XMFLOAT3PK(p[0],p[1],p[2]);
        }
        frame.upload->Unmap(0,nullptr);
    }
    List upload(device.Get()); frame.Upload(upload.cmd.Get()); Hr(upload.cmd->Close()); Submit(queue.Get(),upload);
    Hr(queue->Signal(fence.Get(),++fenceValue)); Wait(fence.Get(),fenceValue);
    void* context=nullptr; LmxxfNrCreateInfo ci{sizeof ci,device.Get(),queue.Get(),argv[2],LMXXF_NR_CREATE_FLAG_RECORDING_LEASES};
    Rc(api.Create(&ci,&context)); Rc(api.PrepareSession(context));
    MochizukiNrControls controls{sizeof controls}; Rc(defaults(&controls));
    controls.drs_mode=0; controls.linear_input=1; controls.max_passes=1; Rc(set(context,&controls));
    MochizukiNrFrameInfo fi{}; fi.struct_size=sizeof fi; fi.color=frame.color.Get(); fi.color_width=width; fi.color_height=height;
    fi.color_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; fi.flags=LMXXF_NR_FRAME_FLAG_STRENGTH;
    fi.transfer_strength=fi.color_strength=fi.model_scale=1; fi.passes=1;
    LmxxfNrJob initial{sizeof initial}; const auto deadline=GetTickCount64()+600000; uint64_t lastLog=0;
    for(;;) {
        int rc=prepare(context,&fi,&initial); if(!rc)break;
        if(rc!=LMXXF_NR_UNAVAILABLE || GetTickCount64()>deadline)Rc(rc);
        if(GetTickCount64()-lastLog>10000) {
            MochizukiNrBuildProgress p{sizeof p}; Rc(progress(context,&p));
            printf("BUILD %s %u/%u\n",p.stage,p.completed,p.total);fflush(stdout);lastLog=GetTickCount64();
        }
        Sleep(50);
    }
    Rc(api.InvalidateRecording(context,initial.handle)); Rc(api.CollectRecording(context,initial.handle));
    if(enable)Rc(enable(context,detailed));
    MochizukiNrInfo shape{sizeof shape}; Rc(info(context,&shape));
    printf("CONFIG %ux%u model=%ux%u depth=%u detailed=%u warmup=%u frames=%u d3d_hz=%llu\n",width,height,shape.model_w,shape.model_h,depth,detailed,warmup,frames,(unsigned long long)frequency);
    std::vector<std::unique_ptr<TimingSlot>> slots;
    for(unsigned i=0;i<depth;++i) slots.push_back(std::make_unique<TimingSlot>(device.Get()));
    FILE* csv=nullptr; Require(_wfopen_s(&csv,argv[3],L"wb")==0,"CSV open");
    fprintf(csv,"serial,block,input_path,total_gpu_ms,d3d_input_ms,d3d_wait_ms,d3d_output_ms,completion_interval_ms,vk_total_ms,vk_upload_ms,core_setup_ms,input_convert_ms,input_encode_ms,pre_network_ms,operators_ms,compose_ms,core_writeback_ms,vk_download_ms,handoff_residual_ms,cpu_prepare_ms,cpu_record_d3d_ms,cpu_enqueue_ms,cpu_submit_ms,cpu_wait_slot_ms,cpu_vk_wait_ms,cpu_vk_record_ms,cpu_vk_submit_ms\n");
    uint64_t serial=0, previousEnd=0;
    unsigned currentMode=1,currentBlock=0;
    auto collect=[&](unsigned slot) {
        auto& s=*slots[slot]; if(!s.tail)return;
        auto waitStart=Clock::now(); Wait(fence.Get(),s.tail); s.wait=Ms(waitStart,Clock::now());
        uint64_t* mapped=nullptr; D3D12_RANGE range{slot*4*sizeof(uint64_t),(slot+1)*4*sizeof(uint64_t)};
        Hr(queryReadback->Map(0,&range,reinterpret_cast<void**>(&mapped)));
        std::array<uint64_t,4> stamps; memcpy(stamps.data(),mapped+slot*4,sizeof stamps);
        D3D12_RANGE empty{0,0}; queryReadback->Unmap(0,&empty);
        const uint64_t end=stamps[detailed?3:1];
        Require(end>stamps[0],"nonpositive D3D timestamp span");
        auto d3d=[&](uint64_t a,uint64_t b) { Require(b>=a,"D3D timestamp order");return double(b-a)*1000/double(frequency); };
        MochizukiProfile p{sizeof p};
        p.cpu_wait_ms=p.cpu_record_ms=p.cpu_submit_ms=NAN;
        if(detailed) { Rc(read(context,s.serial,&p));Require(p.serial==s.serial && p.valid_bits>0,"profile serial mismatch"); }
        auto vk=[&](unsigned a,unsigned b) -> double {
            if(!detailed)return NAN;
            uint64_t mask=p.valid_bits==64?UINT64_MAX:(uint64_t(1)<<p.valid_bits)-1;
            double value=double((p.stamps[b]-p.stamps[a])&mask)*p.timestamp_ns/1e6;
            Require(value>=0 && value<1000,"invalid Vulkan span");return value;
        };
        if(s.serial>warmup) {
            const double input=detailed?d3d(stamps[0],stamps[1]):NAN;
            const double gap=detailed?d3d(stamps[1],stamps[2]):NAN;
            const double output=detailed?d3d(stamps[2],stamps[3]):NAN;
            fprintf(csv,"%llu,%u,%u,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n",
                (unsigned long long)s.serial,s.block,s.mode,d3d(stamps[0],end),input,gap,output,
                previousEnd?d3d(previousEnd,end):NAN,vk(0,9),vk(0,1),vk(1,2),vk(2,3),vk(3,4),vk(4,5),vk(5,6),vk(6,7),vk(7,8),vk(8,9),gap-vk(0,9),
                s.prepare,s.record,s.enqueue,s.submit,s.wait,p.cpu_wait_ms,p.cpu_record_ms,p.cpu_submit_ms);
        }
        previousEnd=end;
        Rc(api.CollectRecording(context,s.job.handle)); s.tail=0;
    };
    auto execute=[&](unsigned slot) {
        auto& s=*slots[slot]; collect(slot);s.mode=currentMode;s.block=currentBlock;
        auto a=Clock::now(); fi.frame_id=++serial;s.serial=serial;s.job={sizeof s.job}; Rc(prepare(context,&fi,&s.job));auto b=Clock::now();
        if(serial>depth) {
            Hr(s.in.allocator->Reset());Hr(s.out.allocator->Reset());
            Hr(s.in.cmd->Reset(s.in.allocator.Get(),nullptr));Hr(s.out.cmd->Reset(s.out.allocator.Get(),nullptr));
        }
        s.in.cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4);
        Rc(api.RecordInputs(context,s.job.handle,s.in.cmd.Get()));
        if(detailed)s.in.cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4+1);
        if(detailed)s.out.cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4+2);
        Rc(api.RecordOutputs(context,s.job.handle,s.out.cmd.Get()));
        s.out.cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4+(detailed?3:1));
        s.out.cmd->ResolveQueryData(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4,detailed?4:2,queryReadback.Get(),slot*4*sizeof(uint64_t));
        Hr(s.in.cmd->Close());Hr(s.out.cmd->Close());auto c=Clock::now();
        Rc(api.BeginRecordingExecution(context,s.job.handle,queue.Get()));Submit(queue.Get(),s.in);auto d=Clock::now();
        Rc(api.EnqueueHip(context,s.job.handle,queue.Get()));auto e=Clock::now();Submit(queue.Get(),s.out);
        s.tail=++fenceValue;HRESULT hr=queue->Signal(fence.Get(),s.tail);
        Rc(api.EndRecordingExecution(context,s.job.handle,queue.Get(),3,fence.Get(),s.tail,hr));
        Rc(api.InvalidateRecording(context,s.job.handle));auto f=Clock::now();
        s.prepare=Ms(a,b);s.record=Ms(b,c);s.enqueue=Ms(d,e);s.submit=Ms(c,d)+Ms(e,f);
    };
    for(unsigned i=0;i<warmup;++i)execute(i%depth);
    for(unsigned i=0;i<depth;++i)collect((warmup+i)%depth);
    auto measuredBegin=Clock::now();
    for(unsigned block=0;block<blocks;++block) {
        currentBlock=block;
        currentMode=blocks==1?1:((block%4==1 || block%4==2)?1:0);
        if(samePath)currentMode=0;
        if(inputMode && blocks>1)Rc(inputMode(context,currentMode?0:1));
        for(unsigned i=0;i<frames;++i)execute((warmup+block*frames+i)%depth);
        for(unsigned i=0;i<depth;++i)collect((warmup+(block+1)*frames+i)%depth);
        printf("BLOCK_DONE %u input_path=%u\n",block,currentMode);fflush(stdout);
    }
    const double wall=Ms(measuredBegin,Clock::now()); fclose(csv);
    // Readback is deliberately outside all timed frames.
    fi.frame_id=++serial;LmxxfNrJob finalJob{sizeof finalJob};Rc(prepare(context,&fi,&finalJob));
    List in(device.Get()),out(device.Get());Rc(api.RecordInputs(context,finalJob.handle,in.cmd.Get()));Rc(api.RecordOutputs(context,finalJob.handle,out.cmd.Get()));
    frame.Read(out.cmd.Get(),static_cast<ID3D12Resource*>(finalJob.private_output));Hr(in.cmd->Close());Hr(out.cmd->Close());
    Rc(api.BeginRecordingExecution(context,finalJob.handle,queue.Get()));Submit(queue.Get(),in);Rc(api.EnqueueHip(context,finalJob.handle,queue.Get()));Submit(queue.Get(),out);
    HRESULT hr=queue->Signal(fence.Get(),++fenceValue);Rc(api.EndRecordingExecution(context,finalJob.handle,queue.Get(),3,fence.Get(),fenceValue,hr));Wait(fence.Get(),fenceValue);
    if(argv[10][0]) {
        FILE* dump=nullptr;Require(_wfopen_s(&dump,argv[10],L"wb")==0,"output dump open");void* pixels=nullptr;Hr(frame.readback->Map(0,nullptr,&pixels));
        for(unsigned y=0;y<height;++y)Require(fwrite(static_cast<char*>(pixels)+y*frame.footprint.Footprint.RowPitch,4,width,dump)==width,"output write");
        frame.readback->Unmap(0,nullptr);fclose(dump);
    }
    Rc(api.InvalidateRecording(context,finalJob.handle));Rc(api.CollectRecording(context,finalJob.handle));Rc(api.Destroy(context));FreeLibrary(dll);
    printf("TIMING_OK frames=%u wall_per_frame_ms=%.6f\n",frames*blocks,wall/(frames*blocks));return 0;
} catch(const std::exception& e) { fprintf(stderr,"TIMING_FAILED %s\n",e.what()); return 1; }
