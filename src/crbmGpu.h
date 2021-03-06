#ifndef CRBMGPU_H
#define CRBMGPU_H

#include "matrix.h"
#include "utilsGpu.h"
#include "crbm.h"

namespace CRBM
{
    class CRBMLayerGpu : public CRBMLayer
    {
        public:
    
            CRBMLayerGpu(void) : CRBMLayer() {}
            CRBMLayerGpu(const CRBMLayerSetting &inSetting);
            
            //main data matrix is in CPU memory
            float LearnAll(const YAMATH::MatrixCpu &inData, const std::string &inBackupFileName = "", bool inputTransposed = false);
            float LearnAll(const YAMATH::MatrixGpu &inData, const std::string &inBackupFileName = "");

            float LearnBatch(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &inOutLastWeights);

            void Transform(const YAMATH::MatrixGpu &inData, YAMATH::MatrixGpu &outData) const;
            void Reconstruct(const YAMATH::MatrixGpu &inData, YAMATH::MatrixGpu &outData);
    
            //x (width), y (height), z (depth or layer count), cx, cy is width and height of convolution filters, stridex/y are shifts of neighbour filters in x and y
            //it is expected that matrix has m.x==x and m.y == y*z
            void Convolve(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const;
            void DeConvolve(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch);
            void DeConvolveRaw(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const;
            void SetDeConvolveNormalizer(int numImages);

            //all parameters are from this layer
            void RawOutput2UpperLayer(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const;
            //all parameters are from this layer as well
            void UpperLayer2RawOutput(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const;
    
            void ActivationFunction(YAMATH::MatrixGpu &inData, int inFunctionType) const;
            void Sample(YAMATH::MatrixGpu &inData, int inFunctionType) const;
    
            virtual void SaveSpecific(std::ostream &outStream) const;
            virtual void LoadSpecific(std::istream &inStream);

            virtual void ResetWeights(void);

            void Test(void);
    
         protected:
            YAMATH::MatrixGpu m_Weights;
            YAMATH::MatrixGpu m_Normalizer;
    };

    CRBMLayerGpu::CRBMLayerGpu(const CRBMLayerSetting &inSetting) :
        CRBMLayer(inSetting)
    {
        ResetWeights();
    }

    void CRBMLayerGpu::ResetWeights(void)
    {
        m_Weights.Reset(s().cx*s().cy*s().z, s().hidden);
        m_Weights.RandNormal(0.0f, 1.0f/(10.0*s().hidden));
        std::cout << "weight matrix randomized!" << std::endl;
    }

    //x (width), y (height), z (depth or layer count), cx, cy is width and height of convolution filters, stridex/y are shifts of neighbour filters in x and y
    //it is expected that matrix has m.x==num.of.images and m.y == x*y*z
    void CRBMLayerGpu::Convolve(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const
    {
        ASSERT(inBatch.getY() == s().x*s().y*s().z);

        //horizontal and vertical number of patches
        int nh, nv;
        getConvolutionPatchesNumber(nh, nv);

        int numImages = inBatch.getX();
        int numPatches = nh*nv;
        int totImages = numPatches*numImages;

        outBatch.Reset(totImages , s().cx*s().cy*s().z);

//#define STREAMS_ON

#ifdef STREAMS_ON
        // allocate and initialize an array of stream handles
        int nstreams = 2;
        std::cout << "async " << nstreams << std::endl;
        cudaStream_t *streams = (cudaStream_t*) malloc(nstreams * sizeof(cudaStream_t));
        for(int i = 0; i < nstreams; i++)
        {
            cudaStreamCreate(&(streams[i]));
        }
        int indexForStream = 0;
#endif //STREAMS_ON

        //99 3 4 1 2 5     one extreme
        for(int ay = 0; ay < s().cy; ++ay)//y in convolution window //3
        {
            for(int ax = 0; ax < s().cx; ++ax)//x in convolution window //4
            {
                for(int py = 0; py < nv; ++py)//y - order of convolution window - patch y //1
                {
                    for(int px = 0; px < nh; ++px)//x - order of convolution window - patch x //2
                    {
                        for(int az = 0; az < s().z; ++az)//image layers //5
                        {
        //45629 5 4 2 1 3  next extreme
        //for(int az = 0; az < s().z; ++az)//image layers //5
        //{
        //    for(int ax = 0; ax < s().cx; ++ax)//x in convolution window //4
        //    {
        //        for(int px = 0; px < nh; ++px)//x - order of convolution window - patch x //2
        //        {
        //            for(int py = 0; py < nv; ++py)//y - order of convolution window - patch y //1
        //            {
        //                for(int ay = 0; ay < s().cy; ++ay)//y in convolution window //3
        //                {
        

#ifdef STREAMS_ON
                            cudaMemcpyAsync
#else //STREAMS_ON
                            cudaMemcpy
#endif //STREAMS_ON

                                (outBatch.getData()  + pixelInColMajor(ax, ay, az, px*numImages + py*nh*numImages, s().cx, s().cy, s().z, totImages) //convolution window target
                                     , inBatch.getDataConst() + pixelInColMajor(s().stridex*px + ax, s().stridey*py + ay, az, 0, s().x, s().y, s().z, numImages) //convolution window source
                                     , sizeof(float)*numImages
                                     , cudaMemcpyDeviceToDevice
#ifdef STREAMS_ON
                                     , streams[(++indexForStream) % nstreams]
#endif //STREAMS_ON

                                     );
                        }
                    }
                }
            }
        }

#ifdef STREAMS_ON
        // release resources
        for(int i = 0; i < nstreams; i++)
        {
            cudaDeviceSynchronize();
            cudaStreamDestroy(streams[i]);
        }
#endif //STREAMS_ON
    }

    void CRBMLayerGpu::SetDeConvolveNormalizer(int numImages)
    {
        //is already propetly set
        if(m_Normalizer.getX() == numImages && m_Normalizer.getY() == s().x*s().y*s().z)
        {
            return;
        }

        m_Normalizer.Reset(numImages , s().x*s().y*s().z);
        m_Normalizer = 0.0f;

        //horizontal and vertical number of patches
        int nh, nv;
        getConvolutionPatchesNumber(nh, nv);

        static int ThreadsPerBlock = 512;
        int blocks = (numImages - 1) / ThreadsPerBlock + 1;

        for(int py = 0; py < nv; ++py)//y - order of convolution window - patch y
        {
            for(int px = 0; px < nh; ++px)//x - order of convolution window - patch x
            {
                for(int ay = 0; ay < s().cy; ++ay)//y in convolution window
                {
                    for(int ax = 0; ax < s().cx; ++ax)//x in convolution window
                    {
                        for(int az = 0; az < s().z; ++az)//image layers
                        {
                            //float *dFrom = getDataConst() + pixelInColMajor(ax, ay, az, px*numImages + py*nh*numImages, cx, cy, z, totImages); //convolution window target
                            float *dTo = m_Normalizer.getData()  + pixelInColMajor(s().stridex*px + ax, s().stridey*py + ay, az, 0, s().x, s().y, s().z, numImages); //convolution window source
                            YAMATH::applyFunction<<<blocks, ThreadsPerBlock>>>(dTo, dTo, numImages, YAMATH::EFE_PlusScalar, 1.0f);
                        }
                    }
                }
            }
        }

        int num = numImages*m_Normalizer.getY();
        blocks = (num - 1) / ThreadsPerBlock + 1;
        YAMATH::applyFunction<<<blocks, ThreadsPerBlock>>>(m_Normalizer.getData(), m_Normalizer.getDataConst(), num, YAMATH::EFE_InverseAndMultiply, 1.0f);
    }

    void CRBMLayerGpu::Test(void)
    {
        int tot = 3*200*200;
        int bat = 10;
        YAMATH::MatrixCpu m(bat, tot), n;
        std::cout << std::endl;
        for(int j = 0; j < bat; ++j)
        {
            for(int i = 0; i < tot; ++i)
            {
                m.getData()[i] = i+j;
                std::cout << " " << i+j;
            }
            std::cout << std::endl;
        }
        YAMATH::MatrixGpu a,b,c;
        a = m;
        Convolve(a, b);
        DeConvolve(b, c);

        n = c;
        std::cout << std::endl;
        for(int j = 0; j < bat; ++j)
        {
            for(int i = 0; i < tot; ++i)
            {
                std::cout << " " << std::fixed << n.getDataConst()[i] - m.getDataConst()[i];
            }
            std::cout << std::endl;
        }

        std::cout << std::endl;
    }

    void CRBMLayerGpu::DeConvolve(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch)
    {
        //msgG("inBatch:", inBatch);
        DeConvolveRaw(inBatch, outBatch);
        //msgG("outBatch (nonnormalized):", outBatch);

        SetDeConvolveNormalizer(outBatch.getX());
        //msgG("normalizer:", m_Normalizer);

        outBatch = outBatch*m_Normalizer;
        //msgG("outBatch (normalized):", outBatch);
    }

    void CRBMLayerGpu::DeConvolveRaw(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const
    {
        //horizontal and vertical number of patches
        int nh, nv;
        getConvolutionPatchesNumber(nh, nv);

        int numImages = inBatch.getX() / (nh*nv);

        ASSERT(inBatch.getY() == s().cx*s().cy*s().z);

        outBatch.Reset(numImages , s().x*s().y*s().z);

        int numPatches = nh*nv;

        int totImages = numPatches*numImages;

        //initial reset to zero
        outBatch = 0.0;

        static int ThreadsPerBlock = 512;
        int blocks = (numImages - 1) / ThreadsPerBlock + 1;

        for(int py = 0; py < nv; ++py)//y - order of convolution window - patch y
        {
            for(int px = 0; px < nh; ++px)//x - order of convolution window - patch x
            {
                for(int ay = 0; ay < s().cy; ++ay)//y in convolution window
                {
                    for(int ax = 0; ax < s().cx; ++ax)//x in convolution window
                    {
                        for(int az = 0; az < s().z; ++az)//image layers
                        {
                            float *dFrom = inBatch.getDataConst() + pixelInColMajor(ax, ay, az, px*numImages + py*nh*numImages, s().cx, s().cy, s().z, totImages); //convolution window target
                            float *dTo = outBatch.getData()  + pixelInColMajor(s().stridex*px + ax, s().stridey*py + ay, az, 0, s().x, s().y, s().z, numImages); //convolution window source
                            YAMATH::parallelMatrixOperationBinary<<<blocks, ThreadsPerBlock>>>(dTo, dFrom, numImages, YAMATH::EFEB_Plus, dTo);
                        }
                    }
                }
            }
        }
    }

    //all parameters are from this layer
    void CRBMLayerGpu::RawOutput2UpperLayer(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const
    {
        //horizontal and vertical number of patches
        int nh, nv;
        getConvolutionPatchesNumber(nh, nv);

        int numImages = (inBatch.getX()*inBatch.getY()) / (nh*nv*s().hidden);
        //msgG("inBatch: ", inBatch);
        //cout << "nh" << nh << std::endl;
        //cout << "nv" << nv << std::endl;
        //cout << "s().hidden" << s().hidden << std::endl;
        //cout << "Num images" << numImages << std::endl;

        int numPatches = nh*nv;
        int total = inBatch.getX()*inBatch.getY();
        int imageAllInOneSize = total/numImages;

        int features = imageAllInOneSize/numPatches;

        outBatch.Reset(numImages, imageAllInOneSize);

//#define STREAMS_ON

#ifdef STREAMS_ON
        // allocate and initialize an array of stream handles
        int nstreams = 14;
        std::cout << "async " << nstreams << std::endl;
        cudaStream_t *streams = (cudaStream_t*) malloc(nstreams * sizeof(cudaStream_t));
        for(int i = 0; i < nstreams; i++)
        {
            cudaStreamCreate(&(streams[i]));
        }
        int indexForStream = 0;
#endif //STREAMS_ON

        //cout << "patches:" << numPatches << std::endl;
        //cout << "features:" << features << std::endl;
        //cout << "images:" << numImages << std::endl;

        for(int p = 0; p < numPatches; ++p)//p - patch number
        {
            for(int f = 0; f < features; ++f)//f - number of features (hidden layer)
            {
                        {
#ifdef STREAMS_ON
                            cudaMemcpyAsync
#else //STREAMS_ON
                            cudaMemcpy
#endif //STREAMS_ON

                                (outBatch.getData() + (f + p*features)*numImages //target
                                     , inBatch.getDataConst() + (f*numPatches + p)*numImages //source
                                     , sizeof(float)*numImages
                                     , cudaMemcpyDeviceToDevice
#ifdef STREAMS_ON
                                     , streams[(++indexForStream) % nstreams]
#endif //STREAMS_ON

                                     );
                            //goto breakit;
                        }
            }
        }
//breakit:

#ifdef STREAMS_ON
        // release resources
        for(int i = 0; i < nstreams; i++)
        {
            cudaDeviceSynchronize();
            cudaStreamDestroy(streams[i]);
        }
#endif //STREAMS_ON
    }

    //all parameters are from this layer as well
    void CRBMLayerGpu::UpperLayer2RawOutput(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &outBatch) const
    {
        //horizontal and vertical number of patches
        int nh, nv;

        getConvolutionPatchesNumber(nh, nv);

        //std::cout << "nh=" << nh << ", nv=" << nv << ", s().hidden=" << s().hidden << ", inBatch.getX()=" << inBatch.getX() << ", inBatch.getY()=" << inBatch.getY() << std::endl;

        int numImages = (inBatch.getX()*inBatch.getY()) / (nh*nv*s().hidden);

        int numPatches = nh*nv;
        int total = inBatch.getX()*inBatch.getY();

        //res must be patches-number*rest ?
        outBatch.Reset(numPatches*numImages, s().hidden);

//#define STREAMS_ON

#ifdef STREAMS_ON
        // allocate and initialize an array of stream handles
        int nstreams = 14;
        std::cout << "async " << nstreams << std::endl;
        cudaStream_t *streams = (cudaStream_t*) malloc(nstreams * sizeof(cudaStream_t));
        for(int i = 0; i < nstreams; i++)
        {
            cudaStreamCreate(&(streams[i]));
        }
        int indexForStream = 0;
#endif //STREAMS_ON

        //TODO: remove
        //outBatch = -1.0;

        //cout << "patches:" << numPatches << std::endl;
        //cout << "features:" << s().hidden << std::endl;
        //cout << "images:" << numImages << std::endl;

        for(int p = 0; p < numPatches; ++p)//p - patch number
        {
            for(int f = 0; f < s().hidden; ++f)//f - number of features (hidden layer)
            {
                        {
#ifdef STREAMS_ON
                            cudaMemcpyAsync
#else //STREAMS_ON
                            cudaMemcpy
#endif //STREAMS_ON

                                (outBatch.getData() + (f*numPatches + p)*numImages //target
                                     , inBatch.getDataConst() + (f + p*s().hidden)*numImages //source
                                     , sizeof(float)*numImages
                                     , cudaMemcpyDeviceToDevice
#ifdef STREAMS_ON
                                     , streams[(++indexForStream) % nstreams]
#endif //STREAMS_ON

                                     );
                            //goto breakit;
                        }
            }
        }
//breakit:

#ifdef STREAMS_ON
        // release resources
        for(int i = 0; i < nstreams; i++)
        {
            cudaDeviceSynchronize();
            cudaStreamDestroy(streams[i]);
        }
#endif //STREAMS_ON

    }

    void testNan(const YAMATH::MatrixGpu &inInp)
    {
        YAMATH::MatrixGpu r;
        r = inInp.Sum();
        YAMATH::MatrixCpu rr = r;

        ASSERT(inInp.getX()*inInp.getY() > 0);
    
        float res = rr.getDataConst()[0]/(inInp.getX()*inInp.getY());

        if(isnan(res) || isinf(res))
        {
            std::cout << "Returned " << res << " when computing error!" << std::endl;

            //YAMATH::MatrixCpu zz = inInp;
            //for(YAMATH::t_index i = 0; i < zz.getX()*zz.getY(); ++i)
            //{
            //    cout << " " << zz.getDataConst()[i];
            //}

            ASSERT(0);
        }
    }

    float computeError(const YAMATH::MatrixGpu &inInp, const YAMATH::MatrixGpu &inOut)
    {
        YAMATH::MatrixGpu r2, r3;
        r2 = inInp - inOut;
        r2 = r2 ^ 2.0f;
        r3 = r2.Sum();
    
        YAMATH::MatrixCpu rr = r3;
    
        float res = sqrt(rr.getDataConst()[0])/(inInp.getX()*inInp.getY());

        //if(res != res)
        if(isnan(res) || isinf(res))
        {
            std::cout << "Returned " << res << " when computing error!" << std::endl;

            YAMATH::MatrixCpu zz = inInp;
            for(YAMATH::t_index i = 0; i < zz.getX()*zz.getY(); ++i)
            {
                std::cout << " " << zz.getDataConst()[i];
            }

            exit(1);
        }

        return res;
    }

    float computeWeightSize(const YAMATH::MatrixGpu &inW)
    {
        YAMATH::MatrixGpu r2, r3;
        r2 = inW;
        r2 = r2 ^ 2.0f;
        r3 = r2.Sum();
    
        YAMATH::MatrixCpu rr = r3;
    
        return rr.getDataConst()[0]/(inW.getX()*inW.getY());
    }

    float CRBMLayerGpu::LearnAll(const YAMATH::MatrixCpu &inData, const std::string &inBackupFileName, bool inputTransposed)
    {
        int transX, transY;//transformed size
        getConvolutionPatchesNumber(transX, transY);

        std::cout << "Main data in CPU memory"<< std::setprecision(6) << std::scientific << std::endl;
        std::cout << "On image " << s().x << "x" << s().y << "x" << s().z << " applied convolution " << s().cx << "x" << s().cy << " with stride " << s().stridex << "x" << s().stridey
             << " => " << transX << "x" << transY << " patches." << std::endl;

        float error = -1;
        YAMATH::MatrixCpu batchCpu;
        YAMATH::MatrixGpu batch;

        YAMATH::MatrixGpu lastWeights(m_Weights.getX(), m_Weights.getY());
        lastWeights = 0.0f;

        for(int i = 1; i <= s().iterations && !IsStopRequired(); ++i)
        {
            Timer t;
            std::cout << i << " / " << s().iterations << " sampling ... " << std::flush;

            if(inputTransposed)
            {
                inData.SampleCols(s().batchSize, batchCpu);

                //transposition needed
                batch = batchCpu;
                testNan(batch);
                batch.Transpose();
                testNan(batch);
                batch.MakeHardCopy();
                testNan(batch);
            }
            else
            {
                inData.Sample(s().batchSize, batchCpu);
                batch = batchCpu;
            }

            testNan(batch);

            t.tac();

            error = LearnBatch(batch, lastWeights);

            if(i % s().saveInterval == 0 && inBackupFileName != "")
            {
                std::stringstream ss;
                ss << inBackupFileName;

                if(s().incrementalSave)
                {
                    m_Setting.incrementalSaveStart += 1;

                    ss << "." << (s().incrementalSaveStart-1);
                }

                Save(ss.str());
            }
        }

        return error;
    }

    float CRBMLayerGpu::LearnAll(const YAMATH::MatrixGpu &inData, const std::string &inBackupFileName)
    {
        int transX, transY;//transformed size
        getConvolutionPatchesNumber(transX, transY);

        std::cout << "Main data in GPU memory" << std::endl;
        std::cout << "On image " << s().x << "x" << s().y << "x" << s().z << " applied convolution " << s().cx << "x" << s().cy << " with stride " << s().stridex << "x" << s().stridey
             << " => " << transX << "x" << transY << " patches." << std::endl;

        float error = -1;
        YAMATH::MatrixGpu batch;

        YAMATH::MatrixGpu lastWeights(m_Weights.getX(), m_Weights.getY());
        lastWeights = 0.0f;

        for(int i = 1; i <= s().iterations && !IsStopRequired(); ++i)
        {
            Timer t;
            std::cout << i << " / " << s().iterations << " sampling ... " << std::flush;
            inData.Sample(s().batchSize, batch);
            t.tac();

            error = LearnBatch(batch, lastWeights);

            if(i % s().saveInterval == 0 && inBackupFileName != "")
            {
                Save(inBackupFileName);
            }
        }

        return error;
    }

    float CRBMLayerGpu::LearnBatch(const YAMATH::MatrixGpu &inBatch, YAMATH::MatrixGpu &inOutLastWeights)
    {
        Timer timer;

        YAMATH::MatrixGpu x, xraw, y, x2, y2, dw1, dw2, dw1a, dw2a, noise;

        //timer.tic();
        std::cout << "    Preparing data ... " << std::flush;
        testNan(inBatch);
        Convolve(inBatch, x);
        timer.tac();

        float mean = 0.0f;
        float dev = 0.0f;

        if(s().noiseCenterRange > 0.0f || s().noiseDevRange > 0.0f)
        {
            //compute mean and dev

            YAMATH::MatrixCpu tmpSum = YAMATH::MatrixGpu(x.Sum());
            mean = tmpSum.getDataConst()[0] / (x.getX()*x.getY());
            YAMATH::MatrixGpu diff = x-mean;
            YAMATH::MatrixGpu squared = diff*diff;
            YAMATH::MatrixCpu tmpDev = YAMATH::MatrixGpu(squared.Sum());
            dev = tmpDev.getDataConst()[0] / (x.getX()*x.getY());

            noise.Reset(x.getX(), x.getY());

            //noiseCenterRange = 0.0f;//computes mean and dev of data. center of noise is range (0-noiseCenterRange)*dev
            //noiseDevRange = 0.0f;//scale of noise is dev*noiseDevRange
        }

        float weightSize = computeWeightSize(m_Weights);
        std::cout << "    Weights' size: [" << weightSize << "]" << std::endl;

        //msgG("x", x);
        //msgG("w", m_Weights);

        timer.tic();

        float error = -1.0f;
        std::cout << "    " << s().batchIterations << " iterations:" << std::flush;

        YAMATH::MatrixCpu normalNoise(2,s().batchIterations);
        normalNoise.RandNormal(0.0f, 1.0f);

        for(int i = 1; i <= s().batchIterations && !IsStopRequired(); ++i)
        {
            testNan(x);
            testNan(m_Weights);
            //add noise
            if(s().noiseCenterRange > 0.0f || s().noiseDevRange > 0.0f)
            {
                float nCenter = normalNoise.get(0, i-1) * dev * s().noiseCenterRange;
                float nDev = normalNoise.get(1, i-1) * dev * s().noiseDevRange;

                noise.RandNormal(nCenter, fabs(nDev));
                noise = noise + x;

                //std::cout << "Noise enter: " << nCenter << ", noise dev: " << nDev << std::endl;
                y = Mult(noise, m_Weights);
            }
            else
            {
                y = Mult(x, m_Weights);
            }
            testNan(y);

            ActivationFunction(y, s().activationFunctionH);
            testNan(y);
            Sample(y, s().activationFunctionH);
            testNan(y);

            x2 = Mult(y, m_Weights.T());
            testNan(x2);
            ActivationFunction(x2, s().activationFunctionV);
            testNan(x2);
            Sample(x2, s().activationFunctionV);
            testNan(y);

            y2 = Mult(x2, m_Weights);
            testNan(y2);
            ActivationFunction(y2, s().activationFunctionH);
            testNan(y2);
            Sample(y2, s().activationFunctionH);
            testNan(y);

            dw1 = Mult(x.T(), y);
            testNan(dw1);
            dw2 = Mult(x2.T(), y2);
            testNan(dw2);

            //std::cout << "lr:" << s().learningRate << std::endl;
            //std::cout << "getx:" << x.getX() << std::endl;
            //std::cout << "lr/getx:" << s().learningRate/x.getX() << std::endl;

            //dw1 *= (s().learningRate/x.getX());
            //dw1a = dw1 * (s().learningRate/x.getX());
            dw1 = dw1 * (s().learningRate/x.getX());
            testNan(dw1);
            //dw1 = dw1a;
            testNan(dw1a);
            dw2 = dw2 * (s().learningRate/x.getX());
            testNan(dw2);

            if(s().decayRate > 0.0f)
            {
                m_Weights *= (1.0f - s().decayRate);
                testNan(m_Weights);
            }

            testNan(m_Weights);
            testNan(dw1);
            //m_Weights = m_Weights + dw1;
            dw1a = m_Weights + dw1;
            m_Weights = dw1a;
            testNan(m_Weights);
            m_Weights = m_Weights - dw2;
            testNan(m_Weights);

            if(s().momentum > 0.0f)
            {
                inOutLastWeights *= s().momentum;
                testNan(inOutLastWeights);
                m_Weights *= (1.0f - s().momentum);
                testNan(m_Weights);

                m_Weights += inOutLastWeights;
                testNan(m_Weights);
                inOutLastWeights = m_Weights;
            }

            if(i % s().logModulo == 0 || i == s().batchIterations || i == 1)
            {
                float lastError = error;
                error = computeError(x, x2);
                if(i != 1)
                {
                    if(lastError < error)
                    {
                        std::cout << " !!!!";
                    }

                    std::cout << ",";
                }

                std::cout << " (" << i << ") " << error << std::flush;
            }
        }
        timer.tac("     ... in ");

        return error;
    }

    void CRBMLayerGpu::ActivationFunction(YAMATH::MatrixGpu &inData, int inFunctionType) const
    {
        switch(inFunctionType)
        {
            case 0: //linear
                break;
            case 1: //sigmoid
                inData = inData.Sigmoid();
                break;
            case 2: //rectified linear
                inData = inData.Minimally(0.0f);
                break;
            default:
                ASSERT(0);// && "unknown activation function ID");
        }
    }

    void CRBMLayerGpu::Sample(YAMATH::MatrixGpu &inData, int inFunctionType) const
    {
        if(s().gibbsSampling)
        {
            switch(inFunctionType)
            {
                case 0: //linear
                    {
                        YAMATH::MatrixGpu rnd(inData.getX(), inData.getY());
                        rnd.RandNormal(0.0f, 0.1f);//TODO: get proper deviation
                        inData = inData + rnd;
                    }
                    break;
                case 1: //sigmoid
                    inData.ProbabilityCoinFlip();
                    break;
                case 2: //rectified linear
                    {
                        YAMATH::MatrixGpu rnd(inData.getX(), inData.getY());
                        rnd.RandNormal(0.0f, 0.1f);//TODO: get proper deviation
                        //YAMATH::MatrixCpu tmp(rnd);
                        //for(int i = 0; i < tmp.getY(); ++i)
                        //{
                        //    std::cout << " " << tmp.get(0, i);
                        //}
                        //std::cout << std::endl;
                        inData = inData + rnd;
                    }
                    break;
                default:
                    assert(0);// && "unknown activation function ID");
            }
        }
    }

//#define T_I Timer TTT;
//#define T_M(x) cudaDeviceSynchronize(); TTT.tac(x ": "); TTT.tic();

#define T_I
#define T_M(x)

    void  CRBMLayerGpu::Transform(const YAMATH::MatrixGpu &inData, YAMATH::MatrixGpu &outData) const
    {
        T_I;
        YAMATH::MatrixGpu x, y;
        T_M("\nInit");

        Convolve(inData, x);
        T_M("Conv");

        y = Mult(x, m_Weights);
        T_M("Mult");

        ActivationFunction(y, s().activationFunctionH);
        T_M("ActFunc");

        RawOutput2UpperLayer(y, outData);
        T_M("Deconv");
    }

    void CRBMLayerGpu::Reconstruct(const YAMATH::MatrixGpu &inData, YAMATH::MatrixGpu &outData)
    {
        YAMATH::MatrixGpu x, y;

        //msgG("inData", inData);

        UpperLayer2RawOutput(inData, y);

        //msgG("y", inData);

        x = Mult(y, m_Weights.T());
        ActivationFunction(x, s().activationFunctionV);

        //msgG("x", x);

        DeConvolve(x, outData);
    }

   
    void CRBMLayerGpu::SaveSpecific(std::ostream &out) const
    {
        sv(out, "weights", m_Weights);
    }

    void CRBMLayerGpu::LoadSpecific(std::istream &in)
    {
        lv(in, "weights", m_Weights);
    }

}//namespace CRBM

#endif //CRBMGPU_H
