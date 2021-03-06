#ifndef TENSOR3D_H
#define TENSOR3D_H
//EIGEN
#include <Eigen/Core>
#include <unsupported/Eigen/CXX11/Tensor>

#include <vector>

//Other
#include "typedefs.h"
#include <boost/filesystem.hpp>
#include "emd_hat.hpp"
#include <math.h>


//#ifdef _OPENMP
//#include <omp.h>
//#endif
#include <omp.h>


typedef float Scalar;

template <typename Type>
class Tensor3D{
public:
    Tensor3D() : tensor( Eigen::Tensor< Type, 3, Eigen::RowMajor>(0,0,0))
    {}


    Tensor3D( Eigen::TensorMap< Eigen::Tensor< Type, 3, Eigen::RowMajor> > t)
        : tensor( t)
    {}

    Tensor3D( Eigen::Tensor< Type, 3, Eigen::RowMajor>& t)
        : tensor( t)
    {}

    Tensor3D( int s1, int s2, int s3)
    {
        tensor = Eigen::Tensor< Type, 3, Eigen::RowMajor>(s1, s2, s3);
    }

    Eigen::Tensor< Type, 3, Eigen::RowMajor>& operator()(){
        return tensor;
    }

    const Eigen::Tensor< Type, 3, Eigen::RowMajor>& operator()() const{
        return tensor;
    }

    int rows() const{
        return tensor.dimension(1);
    }
    int cols() const {
        return tensor.dimension(2);
    }
    int depths() const{
        return tensor.dimension(0);
    }
    int size() const{
        return tensor.size();//rows() * cols() * depths();
    }

    bool isZero() const{
        for (int i = 0; i < depths(); ++i) {
            for (int j = 0; j < rows(); ++j) {
                for (int k = 0; k < cols(); ++k) {
                    if ( tensor(i, j, k) != 0){
                        return false;
                    }
                }
            }
        }
        return true;
    }

    void setZero(){
//#pragma omp parallel for
        for (int z = 0; z < depths(); ++z) {
            for (int y = 0; y < rows(); ++y) {
                for (int x = 0; x < cols(); ++x) {
                    *(tensor(z, y, x))=0;
                }
            }
        }
    }

    //Level
    bool hasNegValue() const{
//#pragma omp parallel for
        for (int z = 0; z < depths(); ++z) {
            for (int y = 0; y < rows(); ++y) {
                for (int x = 0; x < cols(); ++x) {
                    for (int j = 0; j < 352; ++j) {
                        if( tensor(z, y, x)(j) < 0) return true;
                    }
                }
            }
        }
        return false;
    }


    //Level
    Tensor3D<Scalar> RBFconvolve( Tensor3D< Type> filter) const{
//        cout<<"tensor3D::convolve ..."<<endl;

        Tensor3D<Scalar> res( depths() - filter.depths() + 1,
                              rows() - filter.rows() + 1,
                              cols() - filter.cols() + 1);



        res().setConstant( 0);

//        Scalar filterNorm = filter.lvlSquaredNorm();

// Uncomment if you want to normalize the convolution score
//        Type filterMean = filter.sum() / Scalar(filter.size());

#pragma omp parallel for //num_threads(omp_get_max_threads())
        for (int z = 0; z < res.depths(); ++z) {
#pragma omp parallel for
            for (int y = 0; y < res.rows(); ++y) {
#pragma omp parallel for
                for (int x = 0; x < res.cols(); ++x) {

//                    Type tensorMean = agglomerateBlock(z, y, x, filter.depths(), filter.rows(), filter.cols())()(0,0,0) /
//                            Scalar(filter.size());

                    Scalar tensorNorm = block(z,y,x,filter.depths(), filter.rows(), filter.cols()).lvlSquaredNorm();
                    if(!tensorNorm) tensorNorm = 1;

//                    Type squaredNormTensor;
//                    Type squaredNormFilter;
//                    squaredNormTensor.setConstant( 0);
//                    squaredNormFilter.setConstant( 0);
//                    Scalar aux( 0);

                    for (int dz = 0; dz < filter.depths(); ++dz) {
                        for (int dy = 0; dy < filter.rows(); ++dy) {
                            for (int dx = 0; dx < filter.cols(); ++dx) {

                                Type normTensor = tensor(z+dz, y+dy, x+dx) /*- tensorMean*/;
                                Type normFilter = filter()(dz, dy, dx) /*- filterMean*/;

//                                Type sum = filter.sum();
//                                for(int i=0; i<352; ++i){
//                                    if (sum(i) !=0) normFi/*- tensorMean*/lter(i) /= sum(i);
//                                    else normFilter(i) = 0;
//                                }
//                                squaredNormTensor += normTensor * normTensor;
//                                squaredNormFilter += normFilter * normFilter;
//                                aux += normTensor.matrix().dot(normFilter.matrix()) * normTensor.matrix().dot(normFilter.matrix());
                                Type diff = normTensor - normFilter;
                                Scalar norm = 0;
                                for(int i=0; i<352; ++i){
                                    norm += diff(i) * diff(i);
                                }
                                float gamma = 10;

                                res()(z, y, x) += exp(-gamma * norm);

                            }
                        }
                    }
//                    res()(z, y, x) /= (filterNorm /** tensorNorm*/);
//                    res()(z, y, x) /= sqrt(squaredNormTensor.matrix().sum() * squaredNormFilter.matrix().sum());
                }
            }
        }
        return res;
    }


    //Level
    Tensor3D<Scalar> convolve( Tensor3D< Type> filter) const{
//        cout<<"tensor3D::convolve ..."<<endl;

        Tensor3D<Scalar> res( depths() - filter.depths() + 1,
                              rows() - filter.rows() + 1,
                              cols() - filter.cols() + 1);



        res().setConstant( 0);

        Scalar filterNorm = filter.lvlSquaredNorm();

// Uncomment if you want to normalize the convolution score
//        Type filterMean = filter.sum() / Scalar(filter.size());

        #pragma omp parallel for //num_threads(omp_get_max_threads())
        for (int z = 0; z < res.depths(); ++z) {
            #pragma omp parallel for
            for (int y = 0; y < res.rows(); ++y) {
                #pragma omp parallel for
                for (int x = 0; x < res.cols(); ++x) {

//                    Type tensorMean = agglomerateBlock(z, y, x, filter.depths(), filter.rows(), filter.cols())()(0,0,0) /
//                            Scalar(filter.size());

                    Scalar tensorNorm = block(z,y,x,filter.depths(), filter.rows(), filter.cols()).lvlSquaredNorm();
                    if(!tensorNorm) tensorNorm = 1;

//                    Type squaredNormTensor;
//                    Type squaredNormFilter;
//                    squaredNormTensor.setConstant( 0);
//                    squaredNormFilter.setConstant( 0);
//                    Scalar aux( 0);

                    for (int dz = 0; dz < filter.depths(); ++dz) {
                        for (int dy = 0; dy < filter.rows(); ++dy) {
                            for (int dx = 0; dx < filter.cols(); ++dx) {

                                Type normTensor = tensor(z+dz, y+dy, x+dx) /*- tensorMean*/;
                                Type normFilter = filter()(dz, dy, dx) /*- filterMean*/;

//                                Type sum = filter.sum();
//                                for(int i=0; i<352; ++i){
//                                    if (sum(i) !=0) normFi/*- tensorMean*/lter(i) /= sum(i);
//                                    else normFilter(i) = 0;
//                                }
//                                squaredNormTensor += normTensor * normTensor;
//                                squaredNormFilter += normFilter * normFilter;
//                                aux += normTensor.matrix().dot(normFilter.matrix()) * normTensor.matrix().dot(normFilter.matrix());
                                res()(z, y, x) += normTensor.matrix().dot(normFilter.matrix());

                            }
                        }
                    }
//                    res()(z, y, x) /= (filterNorm * tensorNorm);
//                    res()(z, y, x) /= sqrt(squaredNormTensor.matrix().sum() * squaredNormFilter.matrix().sum());
                }
            }
        }
        return res;
    }

    //Level
    /// You should change the sign of the maxScore variable in Mixture::PosLatenSearch()
    /// if you use this function.
    Tensor3D<Scalar> khi2Convolve( Tensor3D< Type> filter) const{
        Tensor3D<Scalar> res( depths() - filter.depths() + 1,
                              rows() - filter.rows() + 1,
                              cols() - filter.cols() + 1);

        res().setConstant( 0);

//#pragma omp parallel for num_threads(omp_get_max_threads())
        for (int z = 0; z < res.depths(); ++z) {
            for (int y = 0; y < res.rows(); ++y) {
                for (int x = 0; x < res.cols(); ++x) {


                    for (int dz = 0; dz < filter.depths(); ++dz) {
                        for (int dy = 0; dy < filter.rows(); ++dy) {
                            for (int dx = 0; dx < filter.cols(); ++dx) {

//                                Type candidate = tensor(z+dz, y+dy, x+dx);
//                                for (int j = 0; j < 352; ++j) {
//                                    Scalar denominator = (abs(filter()(dz, dy, dx)(j)) + abs(candidate(j)));
//                                    if( denominator != 0){
//                                        res()(z, y, x) += (filter()(dz, dy, dx)(j) - candidate(j)) * (filter()(dz, dy, dx)(j) - candidate(j))
//                                                / denominator;
//                                    }
//                                }

                                Type candidate = tensor(z+dz, y+dy, x+dx);
                                Type numerator = (filter()(dz, dy, dx) - candidate) * (filter()(dz, dy, dx) - candidate);
                                Type denominator = abs(filter()(dz, dy, dx)) + abs(candidate);
                                for (int j = 0; j < 352; ++j) {
                                    if( denominator(j) != 0){
                                        res()(z, y, x) += numerator(j) / denominator(j);
                                    }
                                }

                            }
                        }
                    }

                }
            }
        }
        return res;
    }


    //Level
    Tensor3D<Scalar> EMD( Tensor3D< Type> filter) const{

        Tensor3D<Scalar> res( depths() - filter.depths() + 1,
                              rows() - filter.rows() + 1,
                              cols() - filter.cols() + 1);

        res().setConstant( 0);

        // Ground metric definition (certainly highly optimizable)
        vector<double> desc_filter(352);

        std::vector<std::vector<double> > cost_mat;
        int ground_metric_threshold = 3;
        std::vector<double> cost_mat_row(352);
        for (unsigned int i=0; i<352; ++i){
            desc_filter[i] = filter()(0,0,0)(i);
            cost_mat.push_back(cost_mat_row);
        }

        #pragma omp parallel for
        for (int z = 0; z < res.depths(); ++z) {
            #pragma omp parallel for
            for (int y = 0; y < res.rows(); ++y) {
                #pragma omp parallel for
                for (int x = 0; x < res.cols(); ++x) {

                    vector<double> desc_lvl(352);

                    // Naive ground metric
                    for (int i=0; i<352; ++i){
                        desc_lvl[i] = tensor(z, y, x)(i);
                        for (int j=0; j<352; ++j){
                            cost_mat[i][j]= std::min( ground_metric_threshold, abs(i-j));
                        }
                    }

                    res()(z, y, x) = emd_hat_gd_metric<double>()(desc_lvl, desc_filter, cost_mat,
                                                                 ground_metric_threshold);

                }
            }
        }
        return res;
    }



    //Level
    Tensor3D< Type> agglomerateBlock(int z, int y, int x, int p, int q, int r) const{
        if(z+p>depths() || y+q>rows() || x+r>cols() || z < 0 || y < 0 || x < 0){
            cerr<<"agglomerateBlock:: Try to access : "<<z+p<<" / "<<y+q<<" / "<<x+r<<" on matrix size : "
               <<depths()<<" / "<<rows()<<" / "<<cols()<<endl;
            p = std::min(z+p, depths()) - z;
            q = std::min(y+q, rows()) - y;
            r = std::min(x+r, cols()) - x;
        }
        Tensor3D< Type> res(1,1,1);
        res().setConstant( Type::Zero());
//#pragma omp parallel for num_threads(omp_get_max_threads())
        for (int i = z; i < p; ++i) {
            for (int j = y; j < q; ++j) {
                for (int k = x; k < r; ++k) {
                    res()(0,0,0) += tensor(i, j, k);
                }
            }
        }
        for(int j = 0; j < 352; ++j) res()(0,0,0)(j) /= p*q*r;
        return res;
    }


    //Level
    Tensor3D< Type> agglomerate() const{
        Tensor3D< Type> res(1,1,1);
        res().setConstant( Type::Zero());
//#pragma omp parallel for num_threads(omp_get_max_threads())
        for (int z = 0; z < depths(); ++z) {
            for (int y = 0; y < rows(); ++y) {
                for (int x = 0; x < cols(); ++x) {
                    res()(0,0,0) += tensor(z, y, x);
                }
            }
        }
        for(int j = 0; j < 352; ++j) res()(0,0,0)(j) /= depths()*rows()*cols();
        return res;
    }

    //Level
    Scalar dot( const Tensor3D< Type>& sample) const{
        Scalar res = 0;
//#pragma omp parallel for num_threads(omp_get_max_threads())
        for (int z = 0; z < depths(); ++z) {
            for (int y = 0; y < rows(); ++y) {
                for (int x = 0; x < cols(); ++x) {
                    for (int j = 0; j < 352; ++j) {
                        res += tensor(z, y, x)(j) * sample()(z, y, x)(j);
                    }
                }
            }
        }
        return res;
    }

    //Level
    void operator*=( const Scalar& coef){
//#pragma omp parallel for
        for (int z = 0; z < depths(); ++z) {
            for (int y = 0; y < rows(); ++y) {
                for (int x = 0; x < cols(); ++x) {
                    (*this)()(z, y, x) *= coef;
                }
            }
        }
    }

    //Level
    Tensor3D< Type> operator*( const Scalar& coef) const{
        Tensor3D< Type> res = *this;
//#pragma omp parallel for num_threads(omp_get_max_threads())
        for (int z = 0; z < depths(); ++z) {
            for (int y = 0; y < rows(); ++y) {
                for (int x = 0; x < cols(); ++x) {
                    for (int j = 0; j < 352; ++j) {
                        res()(z, y, x)(j) *= coef;
                    }
                }
            }
        }
        return res;
    }

    //Level
    void operator+=( const Tensor3D< Type>& t){
        #pragma omp parallel for /*num_threads(omp_get_max_threads())*/
        for (int z = 0; z < depths(); ++z) {
            #pragma omp parallel for
            for (int y = 0; y < rows(); ++y) {
                #pragma omp parallel for
                for (int x = 0; x < cols(); ++x) {
                    for (int j = 0; j < 352; ++j) {
                        tensor(z, y, x)(j) += t()(z, y, x)(j);
                    }
                }
            }
        }
    }


    Type last() const{
        return tensor(depths()-1, rows()-1, cols()-1);
    }

    //return a block of size (p, q, r) from point (z, y, x)
    Tensor3D< Scalar*> blockLink(int z, int y, int x, int p, int q, int r){
        if(z+p>depths() || y+q>rows() || x+r>cols() || z < 0 || y < 0 || x < 0){
            cerr<<"blockLink:: Try to access : "<<z+p<<" / "<<y+q<<" / "<<x+r<<" on matrix size : "
               <<depths()<<" / "<<rows()<<" / "<<cols()<<endl;
            exit(0);
        }
        Tensor3D< Scalar*> res(p, q, r);
//#pragma omp parallel for
        for (int i = 0; i < p; ++i) {
            for (int j = 0; j < q; ++j) {
                for (int k = 0; k < r; ++k) {
                    res()(i,j,k) = &tensor(z+i, y+j, x+k);
                }
            }
        }
        return res;
    }



    //TODO replace by TensorMap
    //return a block of size (p, q, r) from point (z, y, x)
    Tensor3D< Type> block(int z, int y, int x, int p, int q, int r) const{
        if(z+p>depths() || y+q>rows() || x+r>cols() || z < 0 || y < 0 || x < 0){
            cerr<<"block:: Try to access : "<<z+p<<" / "<<y+q<<" / "<<x+r<<" on matrix size : "
               <<depths()<<" / "<<rows()<<" / "<<cols()<<endl;
            exit(0);
        }
        Tensor3D< Type> t(p, q, r);
//#pragma omp parallel for
        for (int i = 0; i < p; ++i) {
            for (int j = 0; j < q; ++j) {
                for (int k = 0; k < r; ++k) {
                    t()(i,j,k) = tensor(z+i, y+j, x+k);
                }
            }
        }
        return t;
    }

    Eigen::Matrix<Type, 1, Eigen::Dynamic, Eigen::RowMajor> row( int z, int y) const{
        Eigen::Matrix<Type, 1, Eigen::Dynamic, Eigen::RowMajor> res(tensor.dimension(2));
        for (int x = 0; x < tensor.dimension(2); ++x) {
            res( 0, x) = tensor(z, y, x);
        }
        return res;
    }

    Eigen::Matrix<Type, 1, Eigen::Dynamic, Eigen::RowMajor> col( int z, int x) const{
        Eigen::Matrix<Type, 1, Eigen::Dynamic, Eigen::RowMajor> res(tensor.dimension(1));
        for (int y = 0; y < tensor.dimension(1); ++y) {
            res( 0, y) = tensor(z, y, x);
        }
        return res;
    }

    Eigen::Matrix<Type, 1, Eigen::Dynamic, Eigen::RowMajor> depth( int y, int x) const{
        Eigen::Matrix<Type, 1, Eigen::Dynamic, Eigen::RowMajor> res(tensor.dimension(0));
        for (int z = 0; z < tensor.dimension(0); ++z) {
            res( 0, z) = tensor(z, y, x);
        }
        return res;
    }

    Type sum() const{
        Type res = 0;
        for (int i = 0; i < depths(); ++i) {
            for (int j = 0; j < rows(); ++j) {
                for (int k = 0; k < cols(); ++k) {
                    res += tensor(i, j, k);
                }
            }
        }
        return res;
    }

    Scalar lvlSquaredNorm() const{
        Scalar res = 0;
        for (int i = 0; i < depths(); ++i) {
            for (int j = 0; j < rows(); ++j) {
                for (int k = 0; k < cols(); ++k) {
                    for (int l = 0; l < 352; ++l) {
                        res += tensor(i, j, k)(l) * tensor(i, j, k)(l);/* * tensor(i, j, k)(l)*/;
                    }
                }
            }
        }
        return sqrt(res);//pow(res, 1/3.0);
    }

    Type squaredNorm() const{
        Type res = 0;
        for (int i = 0; i < depths(); ++i) {
            for (int j = 0; j < rows(); ++j) {
                for (int k = 0; k < cols(); ++k) {
                    res += tensor(i, j, k) * tensor(i, j, k);
                }
            }
        }
        return res;
    }

    Type max() const{
        if( tensor.size() <= 0){
            return 0;
        }
        Type res = tensor(0,0,0);
        for (int i = 0; i < depths(); ++i) {
            for (int j = 0; j < rows(); ++j) {
                for (int k = 0; k < cols(); ++k) {
                    if( res < tensor(i, j, k)) res = tensor(i, j, k);
                }
            }
        }
        return res;
    }

    Type min() const{
        if( tensor.size() <= 0){
            return Type(0);
        }
        Type res = tensor(0,0,0);
        for (int i = 0; i < depths(); ++i) {
            for (int j = 0; j < rows(); ++j) {
                for (int k = 0; k < cols(); ++k) {
                    if( res > tensor(i, j, k)) res = tensor(i, j, k);
                }
            }
        }
        return res;
    }


    Eigen::Tensor<Type, 3, Eigen::RowMajor> tensor;
};

/// Type of a pyramid level (matrix of cells).
typedef Tensor3D<Scalar> Tensor3DF;
typedef Tensor3D<int> Tensor3DI;

#endif // TENSOR3D_H
