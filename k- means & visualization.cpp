//
//  k- means & visualization.cpp
//  
//
//  Created by Xu Weiqi on 6/10/18.
//

////////////////////////////////////////////////////////////////////////////
//
// Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
//
// Please refer to the NVIDIA end user license agreement (EULA) associated
// with this source code for terms and conditions that govern your use of
// this software. Any use, reproduction, disclosure, or distribution of
// this software and related documentation outside the terms of the EULA
// is strictly prohibited.
//
////////////////////////////////////////////////////////////////////////////

/*
 This example demonstrates how to use the Cuda OpenGL bindings to
 dynamically modify a vertex buffer using a Cuda kernel.
 
 The steps are:
 1. Create an empty vertex buffer object (VBO)
 2. Register the VBO with Cuda
 3. Map the VBO for writing from Cuda
 4. Run Cuda kernel to modify the vertex positions
 5. Unmap the VBO
 6. Render the results using OpenGL
 
 Host code
 */

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include<random>
#include<iostream>

#ifdef _WIN32
#  define WINDOWS_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

// OpenGL Graphics includes
#include <helper_gl.h>
#if defined (__APPLE__) || defined(MACOSX)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <GLUT/glut.h>
#ifndef glutCloseFunc
#define glutCloseFunc glutWMCloseFunc
#endif
#else
#include <GL/freeglut.h>
#endif

// includes, cuda
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

// Utilities and timing functions
#include <helper_functions.h>    // includes cuda.h and cuda_runtime_api.h
#include <timer.h>               // timing functions

// CUDA helper functions
#include <helper_cuda.h>         // helper functions for CUDA error check
#include <helper_cuda_gl.h>      // helper functions for CUDA/GL interop

#include <vector_types.h>

#define MAX_EPSILON_ERROR 10.0f
#define THRESHOLD          0.30f
#define REFRESH_DELAY     10 //ms

////////////////////////////////////////////////////////////////////////////////
// constants
const unsigned int window_width = 512;
const unsigned int window_height = 512;

const unsigned int mesh_width = 512;
const unsigned int mesh_height = 512;


float g_fAnim = 0.0;

//define vertex
typedef struct
{
    float x = 0;
    float y = 0;
    float z = 0;
    long c = NULL;
}point;

struct SVertex
{
    GLfloat x, y, z;
    GLfloat r, g, b;
};

SVertex * Vertices;

struct color_table
{
    float r = 0;
    float g = 0;
    float b = 0;
};

color_table *ct;

long number_of_data = 3000;
std::default_random_engine de(time(0));


// mouse controls
int mouse_old_x, mouse_old_y;
int mouse_buttons = 0;
float rotate_x = 0.0, rotate_y = 0.0;
float translate_z = -3.0;

StopWatchInterface *timer = NULL;

// Auto-Verification Code
int fpsCount = 0;        // FPS count for averaging
int fpsLimit = 1;        // FPS limit for sampling
int g_Index = 0;
float avgFPS = 0.0f;
unsigned int frameCount = 0;
unsigned int g_TotalErrors = 0;
bool g_bQAReadback = false;

#define MAX(a,b) ((a > b) ? a : b)

////////////////////////////////////////////////////////////////////////////////
// declaration, forward
bool runTest(int argc, char **argv);
void cleanup();

// GL functionality
bool initGL(int *argc, char **argv);
void createVBO(GLuint *vbo, struct cudaGraphicsResource **vbo_res,
               unsigned int vbo_res_flags);


// rendering callbacks
void display();
void keyboard(unsigned char key, int x, int y);
void mouse(int button, int state, int x, int y);
void motion(int x, int y);
void timerEvent(int value);
const char *sSDKsample = "simpleGL (VBO)";






//kmeans part

void generate_random_data(point * data, point * centroids, long number_of_data, long number_of_clusters)
{
    
    for (long i = 0; i < number_of_data; i++)
    {
        data[i].c = rand() % number_of_clusters;
        int cc = rand() % number_of_clusters;
        data[i].x = float(rand()) / RAND_MAX + centroids[cc].x;
        data[i].y = float(rand()) / RAND_MAX + centroids[cc].y;
        data[i].z = float(rand()) / RAND_MAX + centroids[cc].z;
        
    }
}


void generate_random_centroids(point * centroids, long number_of_clusters)
{
    
    for (long i = 0; i < number_of_clusters; i++)
    {
        centroids[i].x = float(rand()) / RAND_MAX;
        centroids[i].y = float(rand()) / RAND_MAX;
        centroids[i].z = float(rand()) / RAND_MAX;
    }
}

void generate_normal_data(point * data, point * centroids, long number_of_clusters, long * cluster_volume)
{
    
    std::normal_distribution<float> nd_x(0, 0.1);
    std::normal_distribution<float> nd_y(0, 0.1);
    std::normal_distribution<float> nd_z(0, 0.1);
    
    
    for (int i = 0; i < number_of_data; i++)
    {
        //data[i].c = rand() % number_of_clusters;
        int cc = rand() % number_of_clusters;
        //cluster_volume[data[i].c] += 1;
        data[i].x = nd_x(de) + centroids[cc].x;
        data[i].y = nd_y(de) + centroids[cc].y;
        data[i].z = nd_z(de) + centroids[cc].z;
    }
    
}


//void generate_normal_data(point * data, point * centroids, long number_of_clusters, long * cluster_volume)
//{
//    std::default_random_engine de(time(0));
//    long counter = 0;
//
//    for (int c = 0; c < number_of_clusters; c++)
//    {
//        std::normal_distribution<float> nd_x(centroids[c].x, 0.1);
//        std::normal_distribution<float> nd_y(centroids[c].y, 0.1);
//        std::normal_distribution<float> nd_z(centroids[c].z, 0.1);
//        for (int i = 0; i < cluster_volume[c]; i++)
//        {
//            data[counter].x = nd_x(de);
//            data[counter].y = nd_y(de);
//            data[counter].z = nd_z(de);
//            counter++;
//        }
//
//
//    }
//
//}






void update_centroids(point * data, point * centroids, long *cluster_volume, long number_of_data, const long number_of_clusters)
{
    
    //point temp_sum[number_of_clusters];
    point * temp_sum = new point[number_of_clusters];
    memset(cluster_volume, 0, sizeof(long) * number_of_clusters);
    
    for (long i = 0; i < number_of_data; i++)
    {
        temp_sum[data[i].c].x += data[i].x;
        temp_sum[data[i].c].y += data[i].y;
        temp_sum[data[i].c].z += data[i].z;
        cluster_volume[data[i].c]++;
        
    }
    
    for (long c = 0; c < number_of_clusters; c++)
        //average
        if (cluster_volume[c] != 0)
        {
            temp_sum[c].x /= cluster_volume[c];
            temp_sum[c].y /= cluster_volume[c];
            temp_sum[c].z /= cluster_volume[c];
        }
    
    for (long c = 0; c < number_of_clusters; c++)
    {
        centroids[c].x = temp_sum[c].x;
        centroids[c].y = temp_sum[c].y;
        centroids[c].z = temp_sum[c].z;
    }
    
    //    for (long c = 0; c < number_of_clusters; c++)
    //    {
    //
    //        printf("[sum] cluster: %d, volume: %d, x: %.3f y: %.3f \n",c,cluster_volume[c],temp_sum[c].x, temp_sum[c].y);
    //    }
    
}


void print_data(point * data, long number_of_data)
{
    for (long i = 0; i<number_of_data; i++)
    {
        printf("[%.3f,%.3f,%.3f %d]\n", data[i].x, data[i].y, data[i].z, data[i].c);
    }
    
}
void print_centroids(point * centroids, long number_of_clusters, long * cluster_volume)
{
    for (long c = 0; c < number_of_clusters; c++)
    {
        printf("centroids: %d [%.3f,%.3f,%.3f] volume: %d\n", c, centroids[c].x, centroids[c].y, centroids[c].z, cluster_volume[c]);
    }
    
}

void data2Vertex(point * data, SVertex * Vertices, long number_of_data, color_table * ct)
{
    
    for (int i = 0; i < number_of_data; i++)
    {
        Vertices[i].x = data[i].x;
        Vertices[i].y = data[i].y;
        Vertices[i].z = data[i].z;
        Vertices[i].r = ct[data[i].c].r;
        Vertices[i].g = ct[data[i].c].g;
        Vertices[i].b = ct[data[i].c].b;
        
    }
    
    
}
__global__ void  update_cluster_label(point * d_data, point * d_centroids, long d_number_of_data, long d_number_of_clusters)
{
    //global index
    long idx = threadIdx.y * blockDim.x + threadIdx.x;
    float min = 10000000.0;
    float temp;
    //    long new_label = NULL;
    
    if (idx < d_number_of_data)
    {
        for (long c = 0; c < d_number_of_clusters; c++)
        {
            temp = sqrt(pow(d_data[idx].x - d_centroids[c].x, 2) + pow(d_data[idx].y - d_centroids[c].y, 2) + pow(d_data[idx].z - d_centroids[c].z, 2));
            if (temp < min)
            {
                min = temp;
                d_data[idx].c = c;
            }
            
        }
    }
    
}


////////////////////////////////////////////////////////////////////////////////
// Program main
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    // Create the CUTIL timer
    sdkCreateTimer(&timer);
    
    // First initialize OpenGL context, so we can properly set the GL for CUDA.
    // This is necessary in order to achieve optimal performance with OpenGL/CUDA interop.
    if (false == initGL(&argc, argv))
    {
        return false;
    }
    
    cudaGLSetGLDevice(gpuGetMaxGflopsDeviceId());
    
    // register callbacks
    glutMouseFunc(mouse);
    
#if defined (__APPLE__) || defined(MACOSX)
    atexit(cleanup);
#else
    glutCloseFunc(cleanup);
#endif
    
    //generate number of data
    
    
    const long number_of_clusters = 2;
    long cluster_volume[number_of_clusters] = { 0 };
    ct = new color_table[number_of_clusters];
    
    for (int i = 0; i < number_of_clusters; i++)
    {
        ct[i].r = float(rand()) / RAND_MAX;
        ct[i].g = float(rand()) / RAND_MAX;
        ct[i].b = float(rand()) / RAND_MAX;
    }
    
    
    ////generat random colors
    
    
    
    
    //define centroids
    point centroids[number_of_clusters];
    //    generate_random_centroids(centroids, number_of_clusters);
    centroids[0].x = 0.5;
    centroids[0].y = 0.5;
    centroids[0].z = 0.5;
    centroids[1].x = 0;
    centroids[1].y = 0;
    centroids[1].z = 0;
    //centroids[2].x = -1;
    //centroids[2].y = -1;
    //centroids[2].z = -1;
    //centroids[0].x = 50;
    //centroids[0].y = 20;
    //centroids[0].z = 30;
    //centroids[1].x = 0;
    //centroids[1].y = 0;
    //centroids[1].z = 0;
    //centroids[2].x = -20;
    //centroids[2].y = -20;
    //centroids[2].z = -30;
    
    ////define data
    point * data = new point[number_of_data];
    for (int i = 0; i < number_of_data; i++)
    {
        data[i].c = rand() % number_of_clusters;
        cluster_volume[data[i].c]++;
    }
    
    
    
    //generate_random_data(data, centroids,number_of_data, number_of_clusters);
    generate_normal_data(data, centroids, number_of_clusters, cluster_volume);
    generate_random_centroids(centroids, number_of_clusters);
    
    for (int c = 0; c < number_of_clusters; c++)
    {
        //cluster_volume[c] = number_of_data / number_of_clusters;
        //number_of_data += cluster_volume[c];
        printf("centroid [%d] volume: %d\n", c, cluster_volume[c]);
        printf("color %.3f,%.3f,%.3f\n", ct[c].r, ct[c].g, ct[c].b);
        
    }
    
    
    // create Vertices
    Vertices = new SVertex[number_of_data];
    //data2Vertex(data, Vertices, number_of_data, ct);
    //print_data(data, number_of_data);
    
    
    ////    define cuda data
    point * d_data;
    cudaMalloc(&d_data, sizeof(point) * number_of_data);
    cudaMemcpy(d_data, data, sizeof(point) * number_of_data, cudaMemcpyHostToDevice);
    
    //define cuda cnetroids
    point * d_centroids;
    cudaMalloc(&d_centroids, sizeof(point) * number_of_clusters);
    cudaMemcpy(d_centroids, centroids, sizeof(point) * number_of_clusters, cudaMemcpyHostToDevice);
    
    //    print_data(data, number_of_data);
    print_centroids(centroids, number_of_clusters, cluster_volume);
    
    
    for (long i = 0; i < 10; i++)
    {
        //update
        update_cluster_label << <number_of_data / 1024 + 1, 1024 >> >(d_data, d_centroids, number_of_data, number_of_clusters);
        cudaMemcpy(data, d_data, sizeof(point) * number_of_data, cudaMemcpyDeviceToHost);
        update_centroids(data, centroids, cluster_volume, number_of_data, number_of_clusters);
        printf("iteration %d:\n\n", i);
        print_centroids(centroids, number_of_clusters, cluster_volume);
        cudaMemcpy(d_centroids, centroids, sizeof(point) * number_of_clusters, cudaMemcpyHostToDevice);
    }
    data2Vertex(data, Vertices, number_of_data, ct);
    // start rendering mainloop
    
    glutMainLoop();
    
    return 0;
}

void computeFPS()
{
    frameCount++;
    fpsCount++;
    
    if (fpsCount == fpsLimit)
    {
        avgFPS = 1.f / (sdkGetAverageTimerValue(&timer) / 1000.f);
        fpsCount = 0;
        fpsLimit = (int)MAX(avgFPS, 1.f);
        
        sdkResetTimer(&timer);
    }
    
    char fps[256];
    sprintf(fps, "Cuda GL Interop (VBO): %3.1f fps (Max 100Hz)", avgFPS);
    glutSetWindowTitle(fps);
}

////////////////////////////////////////////////////////////////////////////////
//! Initialize GL
////////////////////////////////////////////////////////////////////////////////
bool initGL(int *argc, char **argv)
{
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowSize(window_width, window_height);
    glutCreateWindow("Cuda GL Interop (VBO)");
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutMotionFunc(motion);
    glutTimerFunc(REFRESH_DELAY, timerEvent, 0);
    
    // initialize necessary OpenGL extensions
    if (!isGLVersionSupported(2, 0))
    {
        fprintf(stderr, "ERROR: Support for necessary OpenGL extensions missing.");
        fflush(stderr);
        return false;
    }
    
    // default initialization
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glDisable(GL_DEPTH_TEST);
    
    // viewport
    glViewport(0, 0, window_width, window_height);
    
    // projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (GLfloat)window_width / (GLfloat)window_height, 0.1, 10.0);
    
    SDK_CHECK_ERROR_GL();
    
    return true;
}



#ifdef _WIN32
#ifndef FOPEN
#define FOPEN(fHandle,filename,mode) fopen_s(&fHandle, filename, mode)
#endif
#else
#ifndef FOPEN
#define FOPEN(fHandle,filename,mode) (fHandle = fopen(filename, mode))
#endif
#endif



////////////////////////////////////////////////////////////////////////////////
//! Display callback
////////////////////////////////////////////////////////////////////////////////
void display()
{
    sdkStartTimer(&timer);
    
    // run CUDA kernel to generate vertex positions
    //runCuda(&cuda_vbo_resource);
    
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // set view matrix
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, 0.0, translate_z);
    glRotatef(rotate_x, 1.0, 0.0, 0.0);
    glRotatef(rotate_y, 0.0, 1.0, 0.0);
    
    // render from the vbo
    //glBindBuffer(GL_ARRAY_BUFFER, vbo);
    //glVertexPointer(4, GL_FLOAT, 0, 0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    
    glVertexPointer(3,   //3 components per vertex (x,y,z)
                    GL_FLOAT,
                    sizeof(SVertex),
                    Vertices);
    //pass the color pointer
    glColorPointer(3,   //3 components per vertex (r,g,b)
                   GL_FLOAT,
                   sizeof(SVertex),
                   &Vertices[0].r);  //Pointer to the first color
    //point size for point mode (press p for that one)
    glPointSize(2.0);
    //glClearColor(0.0, 0.0, 0.0, 0.0);
    
    
    //glColor3f(1.0, 0.0, 0.0);
    //glDrawArrays(GL_POINTS, 0, mesh_width * mesh_height);
    glDrawArrays(GL_POINTS, 0, number_of_data);
    glDisableClientState(GL_VERTEX_ARRAY);
    
    glutSwapBuffers();
    
    g_fAnim += 0.01f;
    
    sdkStopTimer(&timer);
    computeFPS();
}

void timerEvent(int value)
{
    if (glutGetWindow())
    {
        glutPostRedisplay();
        glutTimerFunc(REFRESH_DELAY, timerEvent, 0);
    }
}

void cleanup()
{
    sdkDeleteTimer(&timer);
    
    
}


////////////////////////////////////////////////////////////////////////////////
//! Keyboard events handler
////////////////////////////////////////////////////////////////////////////////
void keyboard(unsigned char key, int /*x*/, int /*y*/)
{
    switch (key)
    {
        case (27):
#if defined(__APPLE__) || defined(MACOSX)
            exit(EXIT_SUCCESS);
#else
            glutDestroyWindow(glutGetWindow());
            return;
#endif
    }
}

////////////////////////////////////////////////////////////////////////////////
//! Mouse event handlers
////////////////////////////////////////////////////////////////////////////////
void mouse(int button, int state, int x, int y)
{
    if (state == GLUT_DOWN)
    {
        mouse_buttons |= 1 << button;
    }
    else if (state == GLUT_UP)
    {
        mouse_buttons = 0;
    }
    
    mouse_old_x = x;
    mouse_old_y = y;
}

void motion(int x, int y)
{
    float dx, dy;
    dx = (float)(x - mouse_old_x);
    dy = (float)(y - mouse_old_y);
    
    if (mouse_buttons & 1)
    {
        rotate_x += dy * 0.2f;
        rotate_y += dx * 0.2f;
    }
    else if (mouse_buttons & 4)
    {
        translate_z += dy * 0.01f;
    }
    
    mouse_old_x = x;
    mouse_old_y = y;
}

