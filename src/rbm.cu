#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <cassert>
#include <string>

using namespace std;

/*
//#define CUDA

#ifdef CUDA
    //#include "deviceGPU.h"
    #include<cublas_v2.h>


#else
    #include "deviceCPU.h"
    typedef ComputingDevice::DeviceCPU dev;
#endif
*/

#include "matrix.h"



using namespace YAMATH;

void loadMatrix(MatrixCpu &inM, const string& filename, bool inTransposed = false)
{
    cout << "loading [" << filename << "] ... " << endl;
    ifstream f(filename.c_str());
    inM.Load(f, inTransposed);
    f.close();
}

void saveMatrix(MatrixCpu &inM, const string &filename)
{
    cout << "saving [" << filename << "] ... " << endl;
    ofstream f(filename.c_str());
    inM.Save(f);
    f.close();
}


void msgC(char * inMsg, const MatrixCpu &x)
{
    int n = x.getX()*x.getY();
    if(n > 400)
    {
        cout << inMsg << ": " << x.getX() << " x " << x.getY()
             << "[ " << (x.getDataConst()[0]) << ", " << (x.getDataConst()[1]) << " ... " << (x.getDataConst()[n-2]) << ", " << (x.getDataConst()[n-1]) << " ]" << endl;
    }
    else if(n == 1)
    {
        cout  << inMsg << ":[" << x.getDataConst()[0] << "]" << flush;
    }
    else
    {
        cout  << inMsg << ":" << endl;
        x.Save(cout);
        cout << endl;
    }
}

void msgG(char * inMsg, const MatrixGpu &inM)
{
    MatrixCpu x = inM;
    msgC(inMsg, x);
}


void ms(char * inMsg, const MatrixGpu &inM)
{
    //msgG(inMsg, inM);
}


void testGpu(int x, int y)
{
    typedef MatrixGpu M;
    typedef MatrixCpu MC;
    
    cout << "GPU -----------" << endl;

    MC ac(x, y);
    for(int i = 0; i < ac.getX()*ac.getY(); ++i)
    {
        ac.getData()[i] = float(i);
    }
    M a = ac;
    msgG("a - init", a);

    //a = 11.0f;
    //msgG("a=11.0f", a);

    M b = a.AbsSum();
    msgG("b=a.AbsSum()", b);

    MC cc(y, 3);
    for(int i = 0; i < cc.getX()*cc.getY(); ++i)
    {
        cc.getData()[i] = 0.0f;
    }
    cc.getData()[0] = 1.0f;
    cc.getData()[y+1] = 1.0f;
    cc.getData()[2*y+2] = 1.0f;
    M c = cc;
    msgG("c", c);

    M d = Mult(a, c);
    msgG("d=a*c", d);

    
}

void testCpu(int x, int y)
{
    typedef MatrixCpu M;

    cout << "CPU -----------" << endl;
    
    M a(x, y);
    msgC("a - init", a);

    //a = 11.0f;
    for(int i = 0; i < a.getX()*a.getY(); ++i)
    {
        a.getData()[i] = 11;
    }
    msgC("a=11.0f", a);

    M b(1,1);
    float sum = 0.0f;
    for(int i = 0; i < a.getX()*a.getY(); ++i)
    {
        sum += a.getData()[i];
    }
    b.getData()[0] = sum;
    msgC("sum=a.AbsSum()", b);
}

//const float x1[] = {1.0f, 0.0f, 0.0f};
//const float t1[] = {1.0f, 0.0f};
//
//const float x2[] = {1.0f, 0.0f, 1.0f};
//const float t2[] = {1.0f, 0.0f};
//
//const float x3[] = {1.0f, 1.0f, 0.0f};
//const float t3[] = {1.0f, 0.0f};
//
//const float x4[] = {1.0f, 1.0f, 1.0f};
//const float t4[] = {0.0f, 1.0f};

typedef MatrixGpu Mat;

float computeError(Mat &inInp, Mat &inOut)
{
    Mat r2, r3;
    r2 = inInp - inOut;
    //msgG("in", inInp);
    //msgG("out", inOut);
    //msgG("r2", r2);
    r2 ^= 2.0f;
    r3 = r2.Sum();
    r3 *= 1.0f / inInp.getX();

    MatrixCpu rr = r3;

    msgG("abssum2", r3);

    return rr.getDataConst()[0];
}

int main(int argc, char** argv)
{
    if(argc != 7 && argc != 8)
    {
        cout << "Too few params!" << endl;
        cout << argv[0] << " input-vector-file input-weight-file hidden-size learning-speed iter batch [cudadevice-id]" << endl;
        exit(1);
    }

    cublasStatus_t stat;
    cublasHandle_t handle;

    stat = cublasCreate(&handle);
    if (stat != CUBLAS_STATUS_SUCCESS) {
        printf ("CUBLAS initialization failed\n");
        return EXIT_FAILURE;
    }

    if(argc > 7)
    {
        cudaSetDevice(atoi(argv[7]));
    }
    int hidden = atoi(argv[3]);
    float lSpeed = atof(argv[4]);
    float iterations = atof(argv[5]);
    int batchSize = atoi(argv[6]);

    MatrixCpu *xCpu = new MatrixCpu();

    loadMatrix(*xCpu, argv[1]);

    int rows = xCpu->getX();
    int cols = xCpu->getY();

    Mat xx = *xCpu;
    msgG("loaded", xx);

    delete xCpu;
    xCpu = new MatrixCpu();

    if(string(argv[2]) != "-")
    {
        loadMatrix(*xCpu, string(argv[2]));
        msgC("w", *xCpu);
    }

    Mat w(xx.getY(), hidden); //init weights
    cout << xCpu->getX() << ", " << xx.getX() << ", " << xCpu->getY() << ", " << hidden << endl;

    if(xCpu->getX() != xx.getY() || xCpu->getY() != hidden)
    {
        w.RandNormal(0.0f, 1.0f/(10*hidden));
        cout << "weight matrix randomized!" << endl;
    }
    else
    {
        w = *xCpu;
        cout << "weight matrix loaded!" << endl;
    }
    msgG("w", w);
    delete xCpu;


    //w = 0.0f;
    ms("w", w);

    Mat x, y, x2, y2, dw1, dw2, err, lastW;
    lastW = w;

    cout << endl;
    
    float minErr = FLT_MAX;
    int minIndex = 0;

    bool ONE_ROW = true;
    
    for(int i = 0; i < iterations; ++i)
    {
        x = xx.Sample(batchSize);

        y = Mult(x, w); // matrixwise -  y.shape = (dataA.x, weights.y) == (dataB.x, dataB.y)
        //msgG("y", y);
        //msgG("y=x*w", y);

        //y = y.Sigmoid();
        //msgG("y", y);

        x2 = Mult(y, w.T());
        //msgG("x2", x2);

        //x2 = x2.Sigmoid();
        //msgG("x2", x2);

        y2 = Mult(x2, w);
        //msgG("y2", y2);

        //y2 = y2.Sigmoid();
        //msgG("y2", y2);

        dw1 = Mult(x.T(), y);
        //msgG("dw1", dw1);
        dw2 = Mult(x2.T(), y2);
        //msgG("dw2", dw2);

        dw1 *= (lSpeed/x.getX());
        dw2 *= (lSpeed/x.getX());

        w = w + dw1;
        w = w - dw2;

        //lastW *= 0.00001;
        //w = w - lastW;

        lastW = w;
        //msgG("w", w);

        ms("w = w + dw", w);

        if(i % 100 == 0 || i+1 == iterations )
        {
            cout << i << ": ";
            float terr = computeError(x, x2);

            cout << "              " << flush;

            if(ONE_ROW)
            {
                cout << "              " << "\r" << flush;
            }
            else
            {
                cout << endl;
            }
            if(terr < minErr)
            {
                minErr = terr;
                minIndex = i;
            }
        }
    }
    cout << endl;

    MatrixCpu res = w;

    msgC("res", res);
    saveMatrix(res, string(argv[1]) + ".weights");

    y = Mult(xx, w);
    //y = y.Sigmoid();
    MatrixCpu resy = y;
    saveMatrix(resy, string(argv[1]) + ".transform");
    x2 = Mult(y, w.T());
    //x2 = x2.Sigmoid();
    MatrixCpu resx = x2;
    saveMatrix(resx, string(argv[1]) + ".reconstruct");


    cout << "done" << endl << "Min. test error = " << minErr << ", iteration = " << minIndex << endl;

    cout << "done" << endl;
    return 0;

}
