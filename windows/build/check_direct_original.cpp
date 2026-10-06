#include "nrvk.hpp"
#include <array>
#include <filesystem>

static void dispatch(VkCommandBuffer cmd, nrvk::Kernel& k, uint32_t w, uint32_t h,
                     const void* push, uint32_t bytes) {
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.layout,0,1,&k.set,0,nullptr);
    vkCmdPushConstants(cmd,k.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,bytes,push);
    vkCmdDispatch(cmd,(w+7)/8,(h+7)/8,1);
}
int main(int argc,char** argv) try {
    if(argc!=2)throw std::runtime_error("runtime shader directory required");
    nrvk::Context ctx;ctx.create();
    printf("GPU %s driver %s\n",ctx.gpu_name.c_str(),ctx.driver_name.c_str());
    const uint32_t w=257,h=129;const size_t count=size_t(w)*h;
    const auto dir=std::filesystem::path(argv[1]);
    uint64_t compared=0;unsigned arrays=0;
    for(auto format:{VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT}) {
        const unsigned bpp=format==VK_FORMAT_B10G11R11_UFLOAT_PACK32?4:8;
        std::vector<uint32_t> input(count*bpp/4);
        uint32_t random=0x13579bdu;
        auto next=[&] {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        if(bpp==4)for(size_t i=0;i<count;++i)
            input[i]=uint32_t(i%1984)|((next()%1984)<<11)|((next()%992)<<22);
        else {
            auto* halves=reinterpret_cast<uint16_t*>(input.data());
            for(size_t i=0;i<count*4;++i) {
                const uint32_t finite=uint32_t(i*251u)%63488u;
                halves[i]=uint16_t(finite<31744u?finite:(finite-31744u)|0x8000u);
            }
        }
        auto source=ctx.image(w,h,format,true,true,false);
        ctx.transition(source,VK_IMAGE_LAYOUT_GENERAL);
        ctx.upload(source,input.data(),input.size()*4);
        auto pa=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,true);
        auto ka=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,true);
        auto pb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,true);
        auto kb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto ma=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto mb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        for(auto* im:{&pa,&ka,&pb})ctx.transition(*im,VK_IMAGE_LAYOUT_GENERAL);
        auto pas=pa,kas=ka,pbs=pb;pas.sampler=kas.sampler=pbs.sampler=VK_NULL_HANDLE;
        nrvk::Kernel encode_a,encode_b,transfer_a,transfer_b;
        encode_a.create(ctx,(dir/"runtime_encode_sampled.spv").string(),{},16,{&pas,&kas,&source});
        encode_b.create(ctx,(dir/"runtime_encode_direct.spv").string(),{},16,{&pbs,&kb,&source});
        transfer_a.create(ctx,(dir/"runtime_transfer.spv").string(),{},36,{&ma,&pa,&ka,&ma});
        transfer_b.create(ctx,(dir/"runtime_transfer.spv").string(),{},36,{&mb,&pb,&source,&mb});
        std::vector<float> model(count*4);
        for(size_t i=0;i<model.size();++i)model[i]=(int(next()%2048)-256)/1024.f;
        std::vector<uint32_t> a(count*4),b(count*4);
        auto compare=[&](nrvk::Context::Image& ai,nrvk::Context::Image& bi,const char* what,float white) {
            ctx.download(ai,a.data(),a.size()*4);ctx.download(bi,b.data(),b.size()*4);
            size_t different=0,first=0;
            for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]) {if(!different)first=i;++different;}
            printf("format=%u white=%.2f image=%s values=%zu differences=%zu first=%zu a=%08x b=%08x\n",unsigned(format),white,what,a.size(),different,first,a[first],b[first]);
            fflush(stdout);
            if(different)throw std::runtime_error("direct sampling changed FP32 bits");
            compared+=a.size();++arrays;
        };
        for(float white:{.01f,1.f,2.f,100.f}) {
            struct Encode {uint32_t w,h;float white,knee;} encode{w,h,white,.75f};
            ctx.one_shot([&](VkCommandBuffer cmd) {
                dispatch(cmd,encode_a,w,h,&encode,sizeof encode);
                dispatch(cmd,encode_b,w,h,&encode,sizeof encode);
            });
            compare(pa,pb,"proxy",white);
            for(const auto& controls:std::array<std::array<float,3>,5>{{{1,1,4},{0,0,1},{2,1,8},{1,2,4},{.5f,4,2}}}) {
                ctx.upload(ma,model.data(),model.size()*4);ctx.upload(mb,model.data(),model.size()*4);
                struct Transfer {uint32_t w,h,mw,mh,passthrough;float detail,colour,ratio,white;}
                    transfer{w,h,w,h,0,controls[0],controls[1],controls[2],white};
                ctx.one_shot([&](VkCommandBuffer cmd) {
                    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
                    dispatch(cmd,transfer_a,w,h,&transfer,sizeof transfer);
                    dispatch(cmd,transfer_b,w,h,&transfer,sizeof transfer);
                });
                compare(ma,mb,"composition",white);
            }
        }
        encode_a.destroy();encode_b.destroy();transfer_a.destroy();transfer_b.destroy();
        for(auto* im:{&source,&pa,&ka,&pb,&kb,&ma,&mb})ctx.destroy(*im);
    }
    ctx.destroy();printf("DIRECT_ORIGINAL_EXACT_OK arrays=%u values=%llu\n",arrays,(unsigned long long)compared);return 0;
} catch(const std::exception& e) {fprintf(stderr,"DIRECT_ORIGINAL_FAILED %s\n",e.what());return 1;}
