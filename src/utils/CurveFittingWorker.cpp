#include "CurveFittingWorker.h"

#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

extern "C" {
#include <PeakingFit/linear_interpolation.h>
#include <PeakingFit/peakfinder.h>
#include <gradfreeOpt.h>
}

#include "utils/CurveFittingUtils.h"

typedef struct
{
    double fs;
    double *phi;
    unsigned int gridSize;
    double *target;
    unsigned int numBands;
    double *tmp;
    double last_mse;
    void* user_data;
} optUserdata;

CurveFittingWorker::CurveFittingWorker(const CurveFittingOptions& _options, QObject* parent) : QObject(parent), options(_options)
{
    freq =_options.frequencyData();
    target =_options.targetData();
    rng_seed = _options.seed();
    rng_density_func = _options.probabilityDensityFunc();
    array_size = _options.dataCount();
    algorithm_type = _options.algorithmType();
    obc_freq = _options.obcFrequencyRange();
    obc_q = _options.obcQRange();
    obc_gain = _options.obcGainRange();
    force_to_oct_grid_conversion = _options.forceLogOctGrid();
    iterations = _options.iterations();
    avg_bw = _options.averageBandwidth();
}

CurveFittingWorker::~CurveFittingWorker()
{
    free(targetList);
    free(flt_freqList);
    free(maximaIndex);
    free(minimaIndex);
    free(flt_fc);
    free(idx);
    free(dFreqDiscontin);
    free(dif);
    free(flt_peak_g);
    free(initialQ);
    free(phi);
    free(initialAns);
    free(low);
    free(up);
    free(tmpDat);
}

void CurveFittingWorker::optimizationHistoryCallback(void *hostData, unsigned int n, double *currentResult, double *currentFval)
{
    Q_UNUSED(n);
    Q_UNUSED(currentResult);
    //CurveFittingWorker *worker = (CurveFittingWorker*)hostData;

    optUserdata *userdata = (optUserdata*)hostData;
    CurveFittingWorker *worker = (CurveFittingWorker*)userdata->user_data;

    emit worker->mseReceived(*currentFval);

    double *fc = currentResult;
    double *Q = currentResult + userdata->numBands;
    double *gain = currentResult + userdata->numBands * 2;
    double b0, b1, b2, a1, a2;
    memset(userdata->tmp, 0, userdata->gridSize * sizeof(double));
    for (unsigned int i = 0; i < userdata->numBands; i++)
    {
        validatePeaking(gain[i], fc[i], Q[i], userdata->fs, &b0, &b1, &b2, &a1, &a2);
        validateMagCal(b0, b1, b2, a1, a2, userdata->phi, userdata->gridSize, userdata->fs, userdata->tmp);
    }

    // Determine if result changed using fuzzy compare
    if(!isApproximatelyEqual<double>(userdata->last_mse, *currentFval))
    {
        emit worker->graphReceived(std::vector<double>(userdata->tmp, userdata->tmp + userdata->gridSize));
    }

    userdata->last_mse = *currentFval;
}

double CurveFittingWorker::peakingCostFunctionMap(double *x, void *usd)
{
    optUserdata *userdata = (optUserdata*)usd;
    double *fc = x;
    double *Q = x + userdata->numBands;
    double *gain = x + userdata->numBands * 2;
    double b0, b1, b2, a1, a2;
    memset(userdata->tmp, 0, userdata->gridSize * sizeof(double));

    for (unsigned int i = 0; i < userdata->numBands; i++)
    {
        validatePeaking(gain[i], fc[i], Q[i], userdata->fs, &b0, &b1, &b2, &a1, &a2);
        validateMagCal(b0, b1, b2, a1, a2, userdata->phi, userdata->gridSize, userdata->fs, userdata->tmp);
    }

    double meanAcc = 0.0;
    for (unsigned int i = 0; i < userdata->gridSize; i++)
    {
        double error = userdata->tmp[i] - userdata->target[i];
        meanAcc += error * error;
    }
    meanAcc = meanAcc / (double)userdata->gridSize;
    return meanAcc;
}

void CurveFittingWorker::preprocess(double *&flt_freqList, double *&target, uint& array_size, int fs, bool force_to_oct_grid_conversion, double avg_bw, bool* is_nonuniform, bool invert){
    uint i;

    if(invert){
        for(uint i = 0; i < array_size; i++)
            target[i] = -target[i];
    }

    // Detect X axis linearity
    // Assume frequency axis is sorted
    double first = flt_freqList[0];
    double last = flt_freqList[array_size - 1];
    double stepLinspace = (last - first) / (double)(array_size - 1);
    double residue = 0.0;
    for (i = 0; i < array_size; i++)
    {
        double vv = fabs((first + i * stepLinspace) - flt_freqList[i]);
        residue += vv;
    }
    residue = residue / (double)array_size;
    char gdType;
    if (residue > 10.0) // Allow margin of error, in fact, non-zero residue hint the grid is nonuniform
    {
        gdType = 1;
    }
    else
    {
        gdType = 0;
    }

    if(is_nonuniform != nullptr){
        *is_nonuniform = gdType == 1;
    }

    // Convert uniform grid to log grid is recommended
    // Nonuniform grid doesn't necessary to be a log grid, but we can force to make it log
    if (force_to_oct_grid_conversion)
    {
        unsigned int detailLinearGridLen;
        double *spectrum;
        double *linGridFreq;
        if (!gdType)
        {
            detailLinearGridLen = array_size;
            spectrum = (double*)malloc(detailLinearGridLen * sizeof(double));
            linGridFreq = (double*)malloc(detailLinearGridLen * sizeof(double));
            for (i = 0; i < detailLinearGridLen; i++)
            {
                spectrum[i] = target[i];
                linGridFreq[i] = i / ((double)detailLinearGridLen) * (fs / 2);
            }
        }
        else
        {
            detailLinearGridLen = max(array_size, 8192); // Larger the value could be take care large(Especially for uniform grid)
            spectrum = (double*)malloc(detailLinearGridLen * sizeof(double));
            linGridFreq = (double*)malloc(detailLinearGridLen * sizeof(double));
            for (i = 0; i < detailLinearGridLen; i++)
            {
                double fl = i / ((double)detailLinearGridLen) * (fs / 2);
                spectrum[i] = linearInterpolationNoExtrapolate(fl, flt_freqList, target, array_size);
                linGridFreq[i] = fl;
            }
        }
        // Init octave grid shrinker
        const double avgBW = avg_bw; //1.005; // Smaller the value less smooth the output gonna be, of course, don't go too large
        unsigned int arrayLen = detailLinearGridLen;
        unsigned int fcLen = getAuditoryBandLen(arrayLen, avgBW);
        unsigned int idxLen = fcLen + 1;
        unsigned int *indexList = (unsigned int*)malloc((idxLen << 1) * sizeof(unsigned int));
        double *levels = (double*)malloc((idxLen + 3) * sizeof(double));
        double *multiplicationPrecompute = (double*)malloc(idxLen * sizeof(double));
        double *shrinkedAxis = (double*)malloc((idxLen + 3) * sizeof(double));
        initInterpolationList(indexList, levels, avgBW, fcLen, arrayLen);
        for (unsigned int i = 0; i < idxLen; i++)
            multiplicationPrecompute[i] = 1.0 / (indexList[(i << 1) + 1] - indexList[(i << 1) + 0]);
        // Do actual axis conversion
        shrinkedAxis[0] = spectrum[0];
        shrinkedAxis[1] = spectrum[1];
        unsigned int i;
        double sum;
        for (i = 0; i < idxLen; i++)
        {
            sum = 0.0;
            for (unsigned int j = indexList[(i << 1) + 0]; j < indexList[(i << 1) + 1]; j++)
                sum += spectrum[j];
            shrinkedAxis[2 + i] = sum * multiplicationPrecompute[i];
        }
        shrinkedAxis[(idxLen + 3) - 1] = shrinkedAxis[(idxLen + 3) - 2];
        double *ascendingIdx = (double*)malloc(arrayLen * sizeof(double));
        for (i = 0; i < arrayLen; i++)
            ascendingIdx[i] = i;
        unsigned int hz18 = 0;
        for (i = 0; i < idxLen + 3; i++)
        {
            double freqIdxUR = levels[hz18 + i] * arrayLen;
            double realFreq = linearInterpolationNoExtrapolate(freqIdxUR, ascendingIdx, linGridFreq, arrayLen);
            hz18 = i;
            if (realFreq >= 18.0)
                break;
        }
        double *newflt_freqList = (double*)malloc((idxLen + 3 - hz18) * sizeof(double));
        double *newTarget = (double*)malloc((idxLen + 3 - hz18) * sizeof(double));
        for (i = 0; i < idxLen + 3 - hz18; i++)
        {
            double freqIdxUR = levels[hz18 + i] * arrayLen;
            newflt_freqList[i] = linearInterpolationNoExtrapolate(freqIdxUR, ascendingIdx, linGridFreq, arrayLen);
            newTarget[i] = shrinkedAxis[hz18 + i];
        }
        free(shrinkedAxis);
        free(ascendingIdx);
        free(linGridFreq);
        array_size = idxLen + 3 - hz18;
        free(levels);
        free(multiplicationPrecompute);
        free(indexList);
        free(spectrum);
        free(flt_freqList);
        free(target);

        flt_freqList = newflt_freqList;
        target = newTarget;
    }

}

void CurveFittingWorker::run()
{
    pcg32x2_random_t PRNG;
    pcg32x2_srandom_r(&PRNG, rng_seed, rng_seed >> 2,
                      rng_seed >> 4, rng_seed >> 6);

    unsigned int i, j;
    unsigned int K = options.populationK();
    unsigned int N = options.populationN();
    double fs = 44100.0;

    flt_freqList = (double*)malloc(array_size * sizeof(double));
    memcpy(flt_freqList, freq, array_size * sizeof(double));
    targetList = (double*)malloc(array_size * sizeof(double));
    memcpy(targetList, target, array_size * sizeof(double));

    preprocess(flt_freqList, targetList, array_size, fs, force_to_oct_grid_conversion, avg_bw, nullptr, options.invertGain());

    // Bound constraints
    double lowFc = obc_freq.first; // Hz
    double upFc = obc_freq.second; // Hz
    double lowQ = obc_q.first; // 0.01 - 1000, higher shaper the filter
    double upQ = obc_q.second; // 0.01 - 1000, higher shaper the filter
    double lowGain = obc_gain.first; // dB
    double upGain = obc_gain.second; // dB

    /* --- VVV This is already pre-calculated in GUI code
    lowGain = targetList[0];
    upGain = targetList[0];
    for (i = 1; i < array_size; i++)
    {
        if (targetList[i] < lowGain)
            lowGain = targetList[i];
        if (targetList[i] > upGain)
            upGain = targetList[i];
    }
    lowGain -= 5.0;
    upGain += 5.0;
    */


    // Parameter estimation
    unsigned int numMaximas, numMinimas;
    maximaIndex = peakfinder_wrapper(targetList, array_size, 0.1, 1, &numMaximas);
    minimaIndex = peakfinder_wrapper(targetList, array_size, 0.1, 0, &numMinimas);
    unsigned int numBands = numMaximas + numMinimas;

    // Model complexity code start
    float modelComplexity = options.modelComplexity(); // 10% - 120%
    unsigned int oldBandNum = numBands;
    numBands = roundf(numBands * modelComplexity / 100.0f);
    double *flt_fc = (double*)malloc(numBands * sizeof(double));
    for (i = 0; i < numMaximas; i++)
        flt_fc[i] = flt_freqList[maximaIndex[i]];
    for (i = numMaximas; i < ((numBands < oldBandNum) ? numBands : oldBandNum); i++)
        flt_fc[i] = flt_freqList[minimaIndex[i - numMaximas]];
    if (numBands > oldBandNum)
    {
        for (i = oldBandNum; i < numBands; i++)
            flt_fc[i] = c_rand(&PRNG) * (upFc - lowFc) + lowFc;
    }
    // Model complexity code end
    unsigned int *idx = (unsigned int*)malloc(numBands * sizeof(unsigned int));
    sort(flt_fc, numBands, idx);

    // Initial value must not get out-of-bound, more importantly.
    // If value go too extreme, NaN will happens when frequency place on either too close to DC and touching/beyond Nyquist
    for (i = 0; i < numBands; i++)
    {
        if (flt_fc[i] < lowFc)
            flt_fc[i] = lowFc;
        if (flt_fc[i] > upFc)
            flt_fc[i] = upFc;
    }
    // Naive way to prevent nearby filters go too close, because they could contribute nothing
    double smallestJump = 0.0;
    double lowestFreq2Gen = 200.0;
    double highestFreq2Gen = 14000.0;
    dFreqDiscontin = (double*)malloc(numBands * sizeof(double));
    dif = (double*)malloc(numBands * sizeof(double));
    unsigned int cnt = 0;
    while (smallestJump <= 20.0)
    {
        derivative(flt_fc, numBands, 1, dFreqDiscontin, dif);
        unsigned int smIdx;
        smallestJump = minArray(dFreqDiscontin, numBands, &smIdx);
        double newFreq = c_rand(&PRNG) * (highestFreq2Gen - lowestFreq2Gen) + lowestFreq2Gen;
        flt_fc[smIdx] = newFreq;
        sort(flt_fc, numBands, idx);

        cnt++;
        if (cnt > 50)
            break;
    }
    free(idx);
    flt_peak_g = (double*)malloc(numBands * sizeof(double));
    for (i = 0; i < numBands; i++)
        flt_peak_g[i] = npointWndFunction(flt_fc[i], flt_freqList, targetList, array_size);
    initialQ = (double*)malloc(numBands * sizeof(double));
    for (i = 0; i < numBands; i++)
    {
        initialQ[i] = fabs(randn_pcg32x2(&PRNG) * (5 - 0.7) + 0.7);
        flt_fc[i] = log10(flt_fc[i]);
    }

    phi = (double*)malloc(array_size * sizeof(double));
    for (i = 0; i < array_size; i++)
    {
        double term1 = sin(M_PI * flt_freqList[i] / fs);
        phi[i] = 4.0 * term1 * term1;
    }
    unsigned int dim = numBands * 3;

    // Local minima avoidance
    double initialLowGain = -1.5;
    double initialUpGain = 1.5;
    double initialLowQ = -0.5;
    double initialUpQ = 0.5;
    double initialLowFc = -log10(2);
    double initialUpFc = log10(2);
    initialAns = (double*)malloc(K * N * dim * sizeof(double));
    for (i = 0; i < K * N; i++)
    {
        for (j = 0; j < numBands; j++)
        {
            initialAns[i * dim + j] = flt_fc[j] + c_rand(&PRNG) * (initialUpFc - initialLowFc) + initialLowFc;
            initialAns[i * dim + numBands + j] = initialQ[j] + c_rand(&PRNG) * (initialUpQ - initialLowQ) + initialLowQ;
            initialAns[i * dim + numBands * 2 + j] = flt_peak_g[j] + c_rand(&PRNG) * (initialUpGain - initialLowGain) + initialLowGain;
        }
    }


    low = (double*)malloc(dim * sizeof(double));
    up = (double*)malloc(dim * sizeof(double));
    for (j = 0; j < numBands; j++)
    {
        low[j] = log10(lowFc); low[numBands + j] = lowQ; low[numBands * 2 + j] = lowGain;
        up[j] = log10(upFc); up[numBands + j] = upQ; up[numBands * 2 + j] = upGain;
    }

    // Cost function data setup
    tmpDat = (double*)malloc(array_size * sizeof(double));
    optUserdata userdat;
    userdat.fs = fs;
    userdat.numBands = numBands;
    userdat.phi = phi;
    userdat.target = targetList;
    userdat.tmp = tmpDat;
    userdat.last_mse = -1;
    userdat.gridSize = array_size;
    userdat.user_data = this;
    void *userdataPtr = (void*)&userdat;

    // Select probability distribution function
    double(*pdf1)(pcg32x2_random_t*) = randn_pcg32x2;
    switch(rng_density_func){
    case CurveFittingOptions::PDF_RANDN_PCG32X2:
        pdf1 = randn_pcg32x2;
        break;
    case CurveFittingOptions::PDF_RAND_TRI_PCG32X2:
        pdf1 = rand_tri_pcg32x2;
        break;
    case CurveFittingOptions::PDF_RAND_HANN:
        pdf1 = rand_hann;
        break;
    }

    // Create optimization history callback
    void *hist_userdata = (void*)userdataPtr;
    void(*optStatus)(void*, unsigned int, double*, double*) = optimizationHistoryCallback;

    // Calculate
    double *output = (double*)malloc(dim * sizeof(double));
    switch(algorithm_type){
    // DE is relatively robust, but require a lot iteration to converge, we then improve DE result using fminsearch
    // fminsearchbnd can be a standalone algorithm, but would high dimension or even some simple curve
    // but overall fminsearchbnd converge faster than other 2 algorithms in current library for current fitting purpose
    case CurveFittingOptions::AT_DIFF_EVOLUTION: {
        double gmin = differentialEvolution(peakingCostFunctionMap, userdataPtr, initialAns, K, N, options.deProbiBound(), dim, low, up, iterations, output, &PRNG, pdf1, optStatus, hist_userdata);
        qDebug("CurveFittingThread: gmin=%lf", gmin);
        break;
    }
    // Standalone fminsearch
    case CurveFittingOptions::AT_FMINSEARCHBND: {
        double fval = fminsearchbnd(peakingCostFunctionMap, userdataPtr, initialAns, low, up, dim, 1e-8, 1e-8, iterations, output, options.fminDimensionAdaptive(), optStatus, hist_userdata);
        qDebug("CurveFittingThread: lval=%lf", fval);
        break;
    }
    // Flower pollination could be as robust as DE, user could also improve FPA result using fminsearch
    case CurveFittingOptions::AT_FLOWERPOLLINATION: {
        double gmin = flowerPollination(peakingCostFunctionMap, userdataPtr, initialAns, low, up, dim, K * N, options.flowerPCond(), options.flowerWeightStep(), iterations, output, &PRNG, pdf1, optStatus, hist_userdata);
        qDebug("CurveFittingThread: gmin=%lf", gmin);
        break;
    }
    case CurveFittingOptions::AT_CHIO: {
        double fval = CHIO(peakingCostFunctionMap, userdataPtr, initialAns, K * N, options.chioMaxSolSurviveEpoch(), options.chioC0(), options.chioSpreadingRate(), dim, low, up, iterations, output, &PRNG, optStatus, hist_userdata);
        qDebug("CurveFittingThread: fval=%lf", fval);
        break;
    }
    case CurveFittingOptions::AT_HYBRID_DE_FMIN: {
        double gmin = differentialEvolution(peakingCostFunctionMap, userdataPtr, initialAns, K, N, options.deProbiBound(), dim, low, up, iterations, output, &PRNG, pdf1, optStatus, hist_userdata);
        qDebug("CurveFittingThread: gmin=%lf", gmin);
        double fval = fminsearchbnd(peakingCostFunctionMap, userdataPtr, output, low, up, dim, 1e-8, 1e-8, options.iterationsSecondary(), output, options.fminDimensionAdaptive(), optStatus, hist_userdata);
        qDebug("CurveFittingThread: fval=%lf", fval);

        break;
    }
    case CurveFittingOptions::AT_HYBRID_FLOWER_FMIN: {
        double gmin = flowerPollination(peakingCostFunctionMap, userdataPtr, initialAns, low, up, dim, K * N, options.flowerPCond(), options.flowerWeightStep(), iterations, output, &PRNG, pdf1, optStatus, hist_userdata);
        qDebug("CurveFittingThread: gmin=%lf", gmin);
        double fval = fminsearchbnd(peakingCostFunctionMap, userdataPtr, output, low, up, dim, 1e-8, 1e-8, options.iterationsSecondary(), output, options.fminDimensionAdaptive(), optStatus, hist_userdata);
        qDebug("CurveFittingThread: fval=%lf", fval);
        break;
    }
    case CurveFittingOptions::AT_HYBRID_CHIO_FMIN: {
        double fval = CHIO(peakingCostFunctionMap, userdataPtr, initialAns, K * N, options.chioMaxSolSurviveEpoch(), options.chioC0(), options.chioSpreadingRate(), dim, low, up, iterations, output, &PRNG, optStatus, hist_userdata);
        qDebug("CurveFittingThread: fval=%lf", fval);
        double fval2 = fminsearchbnd(peakingCostFunctionMap, userdataPtr, output, low, up, dim, 1e-8, 1e-8, options.iterationsSecondary(), output, options.fminDimensionAdaptive(), optStatus, hist_userdata);
        qDebug("CurveFittingThread: fval2=%lf", fval2);
        break;
    }
    }

    // Serialize results
    double *fc = output;
    double *q = output + numBands;
    double *gain = output + numBands * 2;
    for(uint i = 0; i < numBands; i++)
    {
        double trueFreq = pow(10.0, fc[i]);
        double omega = 2.0 * M_PI * trueFreq / fs;
        double bw = (asinh(1.0 / (2.0 * q[i])) * sin(omega)) / (log(2.0) / 2.0 * omega);
        results.append(DeflatedBiquad(FilterType::PEAKING, trueFreq, bw, gain[i]));
    }
    free(output);

    emit finished();
}

QVector<DeflatedBiquad> CurveFittingWorker::getResults() const
{
    return results;
}
