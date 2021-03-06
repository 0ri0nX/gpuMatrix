#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <time.h>
#include <csignal>

using namespace std;

#include "matrix.h"
#include "crbmGpu.h"

using namespace YAMATH;
typedef MatrixGpu Mat;

CRBM::CRBMLayerGpu *abc = NULL;

void signalHandler(int signum)
{
    if(abc != NULL)
    {
        cout << endl;
        cout << "!!! Forcing RBM to interrupt learning ...            !!!" << endl;
        cout << "!!! repeated CTRL+C will stop program without saving !!!" << endl;
        cout << endl;

        abc->SignalStop();

        //clear handler
        signal(SIGINT, SIG_DFL);
    }
    else
    {
        exit(signum);
    }
}


int main(int argc, char** argv)
{
    if(argc != 4 && argc != 5)
    {
        cout << "Too few params!" << endl;
        cout << argv[0] << " setting-file model-file input-vector-file [cudadevice-id]" << endl;
        cout << "\tmodel-file can be \"-\" for random-model initialization." << endl;
        exit(1);
    }

    if(argc > 4)
    {
        int device = atoi(argv[4]);
        cout << "Device ID: " << device << endl;
        cudaSetDevice(device);
    }

    cublasStatus_t stat;
    cublasHandle_t handle;

    cout << "cublas init ..." << flush;
    stat = cublasCreate(&handle);
    if (stat != CUBLAS_STATUS_SUCCESS) {
        printf ("CUBLAS initialization failed\n");
        return EXIT_FAILURE;
    }
    cout << " done" << endl;

    CRBM::CRBMLayerSetting setting;
    setting.loadFromFile(argv[1]);

    //register signal SIGINT and signal handler  
    signal(SIGINT, signalHandler);

    Timer timer;
    if(string(argv[2]) != "-")
    {
        cout << "Loading RBM-layer ... " << flush;
        abc = new CRBM::CRBMLayerGpu(setting);
        abc->Load(string(argv[2]));

        //reset loaded setting
        abc->ResetSetting(setting);
    }
    else
    {
        cout << "Creating RBM-layer ... " << flush;
        abc = new CRBM::CRBMLayerGpu(setting);
        timer.tac("  ... done in ");
    }

    MatrixCpu x;
    //matrix is transposed!!! => pictures are saved one after another - like in the file => row major ~ col major transposed
    //loadMatrix(x, argv[3], true, string(argv[3]) + ".cache");
    MatrixLoaderFile loader(argv[3]);
    loader.LoadComplete(x, true);

    timer.tic();
    abc->LearnAll(x, string(argv[3]) + ".rbm", true);
    timer.tac("learning duration: ");

    if(abc->IsStopRequired())
    {
        cout << endl;
        for(int i = 3; i > 0; --i)
        {
            cout << "\rsave will be started in " << i << flush;
            sleep(1);
        }
        cout << "\rsave will be started now! " << endl;
    }

    abc->Save(string(argv[3]) + ".rbm");

    return 0;
}
