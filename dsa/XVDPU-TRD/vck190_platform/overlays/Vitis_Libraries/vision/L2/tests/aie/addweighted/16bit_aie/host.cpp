/*
 * Copyright 2021 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "graph.cpp"

#include <stdio.h>

#include <stdlib.h>

#include <stdint.h>

#include <fstream>
#include "input.h"
#include "input1.h"
#include "golden.h"

// This is used for the PL Kernels

#include "xrt/xrt.h"

#include "xrt/experimental/xrt_kernel.h"

#define SAMPLES 4096

int sizein = TILE_WINDOW_ELEMENTS;

// Using the Cardano API that call XRT API

#include "adf/adf_api/XRTConfig.h"
extern "C" {
#include <xaiengine.h>
}

static std::vector<char>

load_xclbin(xrtDeviceHandle device, const std::string& fnm)

{
    if (fnm.empty()) throw std::runtime_error("No xclbin specified");

    // load bit stream

    std::ifstream stream(fnm);

    stream.seekg(0, stream.end);

    size_t size = stream.tellg();

    stream.seekg(0, stream.beg);

    std::vector<char> header(size);

    stream.read(header.data(), size);

    auto top = reinterpret_cast<const axlf*>(header.data());

    if (xrtDeviceLoadXclbin(device, top)) throw std::runtime_error("Xclbin loading failed");

    return header;
}

/*
 ******************************************************************************
 * Initalize platform
 ******************************************************************************
 */
// int init_platform(xrtDeviceHandle &dhdl, const std::string& fnm, const axlf **top)
// int init_platform(xrtDeviceHandle &dhdl, const std::string& fnm, xuid_t &m_header_uuid)
int init_platform(xrtDeviceHandle& dhdl, const std::string& fnm)
// const axlf *init_platform(xrtDeviceHandle &dhdl, const std::string& fnm)
{
    printf("DEBUG: init_platform\n");
    //////////////////////////////////////////
    // Open xclbin
    //////////////////////////////////////////
    dhdl = xrtDeviceOpen(0);
    // auto xclbin = load_xclbin(dhdl, "vck190_lab8.xclbin");
    // printf("DEBUG: loading xclbin %s\n",fnm.c_str());
    //    auto xclbin = load_xclbin(dhdl, fnm);
    //    const axlf *top  = reinterpret_cast<const axlf*>(xclbin.data());
    //	m_header_uuid = top->m_header.uuid;
    //	return reinterpret_cast<const axlf*>(xclbin.data());

    return 0;
}

/*
 ******************************************************************************
 * Allocate and initalize data buffer
 ******************************************************************************
 */
int data_buffer_init(xrtDeviceHandle& dhdl,
                     std::vector<xrtBufferHandle>& bufferHandles,
                     int16_t* imgBuffer,
                     int16_t* imgBuffer1,
                     int sizeIn,
                     int sizeOut,
                     std::vector<uint16_t*>& bufferMapped) {
    printf("DEBUG: data_buffer_init\n");
    //////////////////////////////////////////
    // input memory
    // No cache no sync seems not working. Should ask SSW team to investigate.
    //////////////////////////////////////////

    xrtBufferHandle in_bohdl = xrtBOAlloc(dhdl, sizeIn * sizeof(int16_t), 0, 0);
    auto in_bomapped = reinterpret_cast<uint16_t*>(xrtBOMap(in_bohdl));
    memcpy(in_bomapped, imgBuffer, sizeIn * sizeof(int16_t));
    printf("Input memory virtual addr 0x%llu\n", in_bomapped);

    xrtBufferHandle in_bohdl1 = xrtBOAlloc(dhdl, sizeIn * sizeof(int16_t), 0, 0);
    auto in_bomapped1 = reinterpret_cast<uint16_t*>(xrtBOMap(in_bohdl1));
    memcpy(in_bomapped1, imgBuffer1, sizeIn * sizeof(int16_t));
    printf("Input memory virtual addr 0x%llu\n", in_bomapped1);

    //////////////////////////////////////////
    // output memory
    //////////////////////////////////////////

    xrtBufferHandle out_bohdl = xrtBOAlloc(dhdl, sizeOut * sizeof(int16_t), 0, 0);
    auto out_bomapped = reinterpret_cast<uint16_t*>(xrtBOMap(out_bohdl));
    memset(out_bomapped, 0xABCDEF00, sizeOut * sizeof(int16_t));
    printf("Output memory virtual addr 0x%llu\n", out_bomapped);

    bufferHandles.push_back(in_bohdl);
    bufferHandles.push_back(in_bohdl1);
    bufferHandles.push_back(out_bohdl);
    bufferMapped.push_back(in_bomapped);
    bufferMapped.push_back(in_bomapped1);
    bufferMapped.push_back(out_bomapped);

    return 0;
}

/*
 ******************************************************************************
 * Configure datamover MM2S
 ******************************************************************************
 */
xrtRunHandle dm_mm2s(xrtKernelHandle& mm2s_khdl, xrtBufferHandle& bufferHandle) {
    xrtRunHandle mm2s_rhdl = xrtRunOpen(mm2s_khdl);
    int rval = xrtRunSetArg(mm2s_rhdl, 0, bufferHandle);
    rval = xrtRunSetArg(mm2s_rhdl, 2, sizein);
    printf("dm_mm2s_config\n");
    return mm2s_rhdl;
}
/*
 ******************************************************************************
 * Configure datamover MM2S
 ******************************************************************************
 */
xrtRunHandle dm_mm2s1(xrtKernelHandle& mm2s_khdl1, xrtBufferHandle& bufferHandle) {
    xrtRunHandle mm2s_rhdl1 = xrtRunOpen(mm2s_khdl1);
    int rval = xrtRunSetArg(mm2s_rhdl1, 0, bufferHandle);
    rval = xrtRunSetArg(mm2s_rhdl1, 2, sizein);
    printf("dm_mm2s1_config\n");
    return mm2s_rhdl1;
}

/*
 ******************************************************************************
 * Configure S2MM datamover
 ******************************************************************************
 */
xrtRunHandle dm_s2mm(xrtKernelHandle& s2mm_khdl, xrtBufferHandle& bufferHandle) {
    xrtRunHandle s2mm_rhdl = xrtRunOpen(s2mm_khdl);
    int rval = xrtRunSetArg(s2mm_rhdl, 0, bufferHandle);
    rval = xrtRunSetArg(s2mm_rhdl, 2, sizein);
    printf("dm_S2MM_config\n");
    return s2mm_rhdl;
}
/*
 ******************************************************************************
 * Run datamover (dm) once
 ******************************************************************************
 */
int dm_run_once(xrtRunHandle& rhdl) {
    xrtRunStart(rhdl);
    return 0;
}

/*
 ******************************************************************************
 * Wait for datamovers (dm) to be done
 ******************************************************************************
 */
int dm_wait_done(xrtRunHandle& rhdl) {
    auto state = xrtRunWait(rhdl);
    std::cout << "Completed with status(" << state << ")\n";
    xrtRunClose(rhdl);
    return 0;
}
/*
 ******************************************************************************
 * Run graph portion of design (AIE/ adf)
 * This function includes configuring the datamovers for each data tile but
 * is a separate step from the tiling/ stitcher functions.
 ******************************************************************************
 */
int graph_run(xrtDeviceHandle& dhdl, const axlf* top, std::vector<xrtBufferHandle>& bufferHandles) {
    adf::registerXRT(dhdl, top->m_header.uuid);
    // adf::registerXRT(dhdl, m_header_uuid);

    //////////////////////////////////////////
    // graph execution for AIE
    //////////////////////////////////////////
    printf("graph init. This does nothing because CDO in boot PDI already configures AIE.\n");
    float alpha = 2.0f;
    float beta = 3.0f;
    float gamma = 4.0f;
    mygraph.init();
    mygraph.update(mygraph.alpha, alpha);
    mygraph.update(mygraph.beta, beta);
    mygraph.update(mygraph.gamma, gamma);

    printf("graph run(%u)\n", 1);
    mygraph.run(1);

    // TODO tiler and stitcher init
    xrtKernelHandle mm2s_khdl = xrtPLKernelOpen(dhdl, top->m_header.uuid, "mm2s");
    xrtKernelHandle mm2s_khdl1 = xrtPLKernelOpen(dhdl, top->m_header.uuid, "bmm2s");
    xrtKernelHandle s2mm_khdl = xrtPLKernelOpen(dhdl, top->m_header.uuid, "s2mm");

    xrtRunHandle mm2s_rhdl = dm_mm2s(mm2s_khdl, bufferHandles[0]);
    xrtRunHandle mm2s_rhdl1 = dm_mm2s1(mm2s_khdl1, bufferHandles[1]);
    xrtRunHandle s2mm_rhdl = dm_s2mm(s2mm_khdl, bufferHandles[2]);

    printf("Enable datamovers\n");
    dm_run_once(mm2s_rhdl);
    dm_run_once(mm2s_rhdl1);
    dm_run_once(s2mm_rhdl);

    printf("Wait for datamovers to complete\n");
    dm_wait_done(mm2s_rhdl);
    dm_wait_done(mm2s_rhdl1);
    dm_wait_done(s2mm_rhdl);

    // TODO tiler and stitcher destructor?
    // xrtKernelClose(mm2s_khdl);
    // xrtKernelClose(mm2s_khdl1);
    // xrtKernelClose(s2mm_khdl);

    // TODO This causes the run to hang. Note sure why???
    mygraph.end();
    // printf("graph end\n");

    return 0;
}

/*
 ******************************************************************************
 * Run test
 * 2x filter2D in sequence
 ******************************************************************************
 */
int run_test(xrtDeviceHandle& dhdl, const axlf* top, std::vector<xrtBufferHandle>& bufferHandles) {
    graph_run(dhdl, top, bufferHandles);
    return 0;
}

/*
 ******************************************************************************
 * Clean up XRT
 ******************************************************************************
 */
int cleanup_platform(xrtDeviceHandle& dhdl,
                     std::vector<xrtBufferHandle>& bufferHandles,
                     std::vector<uint16_t*>& bufferMapped) {
    std::cout << "Releasing remaining XRT objects...\n";
    xrtBOFree(bufferHandles[0]);
    xrtBOFree(bufferHandles[1]);
    xrtBOFree(bufferHandles[2]);
    xrtDeviceClose(dhdl);

    return 0;
}

int main(int argc, char** argv)

{
    try {
        xrtDeviceHandle dhdl;
        init_platform(dhdl, "kernel.xclbin");

        //////////////////////////////////////////

        // Open xclbin

        //////////////////////////////////////////
        auto xclbin = load_xclbin(dhdl, "kernel.xclbin");
        const axlf* top = reinterpret_cast<const axlf*>(xclbin.data());

        std::vector<xrtBufferHandle> bufferHandles;
        std::vector<uint16_t*> bufferMapped;

        //////////////////////////////////////////

        // input memory

        // Allocating the input size of sizeIn to MM2S

        // This is using low-level XRT call xrtBOAlloc to allocate the memory

        //////////////////////////////////////////
        data_buffer_init(dhdl, bufferHandles, input_data, input_data1, sizein, sizein, bufferMapped);

        run_test(dhdl, top, bufferHandles);

        int errorCount = 0;

        {
            for (int i = SMARTTILE_ELEMENTS; i < sizein; i++)

            {
                if (bufferMapped[2][i] != golden[i])

                {
                    printf("Error found @ %d, %d != %d\n", i, bufferMapped[2][i], golden[i]);

                    errorCount++;
                }
            }

            if (errorCount)

                printf("Test failed with %d errors\n", errorCount);

            else

                printf("Test passed\n");
        }

        cleanup_platform(dhdl, bufferHandles, bufferMapped);

        return errorCount;
    } catch (std::exception& e) {
        const char* errorMessage = e.what();
        std::cerr << "Exception caught: " << errorMessage << std::endl;
        exit(-1);
    }
}
