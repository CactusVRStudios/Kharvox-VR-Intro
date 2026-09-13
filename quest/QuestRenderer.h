#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "QuestAssets.h"
#include "../src/intro/CubeScene.h"
#include <cstring>
#include <cstddef>
namespace quest {
class Renderer {
    GLuint program{},buffer{},vao{},framebuffer{},palette{};
    GLsizei count{};size_t capacity{};
    GLint inverseEye{},anchorYaw{},eyeTime{},tangents{};
    static GLuint shader(GLenum type,const char* source){
        GLuint result=glCreateShader(type);glShaderSource(result,1,&source,nullptr);glCompileShader(result);
        GLint ok;glGetShaderiv(result,GL_COMPILE_STATUS,&ok);
        if(!ok){char error[2048]{};glGetShaderInfoLog(result,sizeof(error),nullptr,error);glDeleteShader(result);throw std::runtime_error(error);}
        return result;
    }
public:
    void initialize(){
        const char* vs=R"(#version 300 es
precision highp float;
layout(location=0) in vec3 center;
layout(location=1) in vec2 extent;
layout(location=2) in vec4 color;
layout(location=3) in float mode;
uniform vec4 inverseEye,anchorYaw,eyeTime,tangents;
out vec4 tint;
out float localX;
flat out float kind;
vec3 rotateEye(vec3 p){vec3 t=2.0*cross(inverseEye.xyz,p);return p+inverseEye.w*t+cross(inverseEye.xyz,t);}
void main(){
    const vec2 corners[6]=vec2[6](vec2(-.5,.5),vec2(.5,.5),vec2(-.5,-.5),vec2(-.5,-.5),vec2(.5,.5),vec2(.5,-.5));
    vec2 offset=corners[gl_VertexID]*extent;vec3 local=center;
    if(mode<1.5)local.xy+=offset;
    float sn=sin(anchorYaw.w),cs=cos(anchorYaw.w);
    vec3 world=anchorYaw.xyz+vec3(cs*local.x+sn*local.z,local.y,-sn*local.x+cs*local.z);
    vec3 camera=rotateEye(world-eyeTime.xyz);
    if(mode>1.5)camera.xy+=offset;
    float depth=-camera.z;
    gl_Position=vec4((2.0*camera.x-(tangents.y+tangents.x)*depth)/(tangents.y-tangents.x),
        (2.0*camera.y-(tangents.z+tangents.w)*depth)/(tangents.z-tangents.w),depth-.1,depth);
    tint=color;localX=local.x;kind=mode;
})";
        const char* fs=R"(#version 300 es
precision highp float;
in vec4 tint;
in float localX;
flat in float kind;
uniform sampler2D copper;
uniform vec4 eyeTime;
out vec4 outputColor;
void main(){outputColor=kind==1.0?vec4(texture(copper,vec2(localX*1.25-eyeTime.w*1.2,.5)).rgb,1):tint;}
)";
        auto vertex=shader(GL_VERTEX_SHADER,vs),fragment=shader(GL_FRAGMENT_SHADER,fs);
        program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
        glDeleteShader(vertex);glDeleteShader(fragment);
        GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){char error[2048]{};glGetProgramInfoLog(program,sizeof(error),nullptr,error);throw std::runtime_error(error);}
        inverseEye=glGetUniformLocation(program,"inverseEye");anchorYaw=glGetUniformLocation(program,"anchorYaw");
        eyeTime=glGetUniformLocation(program,"eyeTime");tangents=glGetUniformLocation(program,"tangents");
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        const int sizes[]={3,2,4,1};const size_t offsets[]={0,12,20,36};
        static_assert(sizeof(kharvox::intro::Quad)==44);
        for(int i=0;i<4;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,44,(void*)offsets[i]);glVertexAttribDivisor(i,1);}
        glGenFramebuffers(1,&framebuffer);glGenTextures(1,&palette);glBindTexture(GL_TEXTURE_2D,palette);
        const auto bytes=readAsset("copper.bin");uint32_t size[2]{};
        if(bytes.size()<8)throw std::runtime_error("Missing copper data");std::memcpy(size,bytes.data(),8);
        if(!size[0]||!size[1]||size[0]>4096||size[1]>4096||bytes.size()!=8+size[0]*size[1]*4)throw std::runtime_error("Invalid copper data");
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size[0],size[1],0,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data()+8);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GLES renderer initialization failed");
    }
    size_t prepare(const kharvox::intro::Scene& scene,float time,float beatAge){
        const auto quads=scene.geometry(time,beatAge);count=GLsizei(quads.size());
        glBindBuffer(GL_ARRAY_BUFFER,buffer);
        if(quads.size()>capacity){capacity=std::max(size_t(4096),quads.size()*2);glBufferData(GL_ARRAY_BUFFER,capacity*sizeof(quads[0]),nullptr,GL_STREAM_DRAW);}
        if(count)glBufferSubData(GL_ARRAY_BUFFER,0,quads.size()*sizeof(quads[0]),quads.data());
        return quads.size();
    }
    void draw(GLuint texture,int width,int height,const kharvox::intro::Scene& scene,const XrView& eye,float time,bool black){
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete OpenXR framebuffer");
        glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        if(!black&&count){
            glUseProgram(program);glBindVertexArray(vao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,palette);
            glUniform4f(inverseEye,-eye.pose.orientation.x,-eye.pose.orientation.y,-eye.pose.orientation.z,eye.pose.orientation.w);
            glUniform4f(anchorYaw,scene.anchor.x,scene.anchor.y,scene.anchor.z,scene.yaw);
            glUniform4f(eyeTime,eye.pose.position.x,eye.pose.position.y,eye.pose.position.z,time);
            glUniform4f(tangents,tanf(eye.fov.angleLeft),tanf(eye.fov.angleRight),tanf(eye.fov.angleUp),tanf(eye.fov.angleDown));
            glDrawArraysInstanced(GL_TRIANGLES,0,6,count);
        }
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GLES frame failed");
    }
    void shutdown(){
        if(program)glDeleteProgram(program);if(buffer)glDeleteBuffers(1,&buffer);if(vao)glDeleteVertexArrays(1,&vao);
        if(framebuffer)glDeleteFramebuffers(1,&framebuffer);if(palette)glDeleteTextures(1,&palette);
        program=buffer=vao=framebuffer=palette=0;
    }
};
}
