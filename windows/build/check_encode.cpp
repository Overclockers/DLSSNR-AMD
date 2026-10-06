#include "nrvk.hpp"
#include <array>
#include <filesystem>

static void dispatch(VkCommandBuffer cmd, nrvk::Kernel& k, uint32_t w, uint32_t h, float white) {
    struct { uint32_t w,h;float white,knee; } push{w,h,white,.75f};
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.layout,0,1,&k.set,0,nullptr);
    vkCmdPushConstants(cmd,k.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,&push);
    vkCmdDispatch(cmd,(w+7)/8,(h+7)/8,1);
}
int main(int argc,char** argv) try {
    if(argc!=2)throw std::runtime_error("shader directory required");
    nrvk::Context ctx;ctx.create();
    printf("GPU %s driver %s\n",ctx.gpu_name.c_str(),ctx.driver_name.c_str());
    const uint32_t w=2258,h=1270;const size_t count=size_t(w)*h;
    const auto dir=std::filesystem::path(argv[1]);
    for(auto format:{VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT}) {
        const unsigned bpp=format==VK_FORMAT_B10G11R11_UFLOAT_PACK32?4:8;
        std::vector<uint32_t> input(count*bpp/4);
        uint32_t random=0x13579bd;
        auto next=[&] {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        if(bpp==4) for(size_t i=0;i<count;++i) {
            uint32_t r=uint32_t(i%1984),g=next()%1984,b=next()%992;
            input[i]=r|(g<<11)|(b<<22);
        } else {
            auto* halves=reinterpret_cast<uint16_t*>(input.data());
            for(size_t i=0;i<count*4;++i) {
                uint32_t bits=i%4==0?uint32_t(i/4):next();
                halves[i]=uint16_t((bits%0x7c00)|((bits&0x10000)?0x8000:0));
            }
        }
        auto source=ctx.image(w,h,format,true,true,false);ctx.transition(source,VK_IMAGE_LAYOUT_GENERAL);ctx.upload(source,input.data(),input.size()*4);
        auto pa=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto ka=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto pb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto kb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        nrvk::Kernel base,candidate;
        base.create(ctx,(dir/"runtime_encode.spv").string(),{},16,{&pa,&ka});
        candidate.create(ctx,(dir/"runtime_encode_sampled.spv").string(),{},16,{&pb,&kb,&source});
        std::vector<uint32_t> a(count*4),b(count*4);
        for(float white:{.01f,1.f,2.f,100.f}) {
            ctx.one_shot([&](VkCommandBuffer cmd) {
                VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
                blit.srcOffsets[1]=blit.dstOffsets[1]={int32_t(w),int32_t(h),1};
                vkCmdBlitImage(cmd,source.handle,VK_IMAGE_LAYOUT_GENERAL,pa.handle,VK_IMAGE_LAYOUT_GENERAL,1,&blit,VK_FILTER_NEAREST);
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
                dispatch(cmd,base,w,h,white);dispatch(cmd,candidate,w,h,white);
            });
            for(unsigned which=0;which<2;++which) {
                ctx.download(which?ka:pa,a.data(),a.size()*4);ctx.download(which?kb:pb,b.data(),b.size()*4);
                size_t different=0,first=0;
                for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]) {if(!different)first=i;++different;}
                printf("format=%u white=%.2f image=%s values=%zu differences=%zu first=%zu a=%08x b=%08x\n",unsigned(format),white,which?"keep":"proxy",a.size(),different,first,a[first],b[first]);fflush(stdout);
                if(different)throw std::runtime_error("encode changed FP32 bits");
            }
        }
        base.destroy();candidate.destroy();for(auto* im:{&source,&pa,&ka,&pb,&kb})ctx.destroy(*im);
    }
    ctx.destroy();puts("ENCODE_EXACT_OK");return 0;
} catch(const std::exception& e) {fprintf(stderr,"ENCODE_FAILED %s\n",e.what());return 1;}
