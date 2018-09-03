// Written by Fisichella Thomas
// Date 25/05/2018

#ifndef FFLD_GSHOTPYRAMID_H
#define FFLD_GSHOTPYRAMID_H

//EIGEN
#include <Eigen/Core>
#include <unsupported/Eigen/CXX11/Tensor>

//PCL
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/kdtree/impl/kdtree_flann.hpp>
#include <pcl/features/board.h>
#include <pcl/filters/uniform_sampling.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/features/shot_omp.h>
#include <pcl/common/common_headers.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/io/pcd_io.h>
#include<pcl/io/ply_io.h>
#include <vector>

//Other
#include "typedefs.h"
#include "tensor3d.h"
#include <boost/filesystem.hpp>



namespace FFLD
{
    class GSHOTPyramid
    {
        public:
        /** DEFINITION OF TYPES **/
        
        /// Number of SHOT features (guaranteed to be even). Fixed at compile time for both ease of use
        /// and optimal performance.
        static const int DescriptorSize = 352;
        typedef float Scalar;

        
        /// Type of a matrix.
        typedef Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> Matrix;

        /// Type of a pyramid level cell (fixed-size array of length NbFeatures).
        typedef Eigen::Array<Scalar, DescriptorSize, 1> Cell;

        /// Type of a pyramid level (matrix of cells).
        typedef Tensor3D<Cell> Level;
        
                
        /** CONSTRUCTORS **/
        
        /// Constructs an empty pyramid. An empty pyramid has no level.
        GSHOTPyramid();
        
        // Copy constructor
        GSHOTPyramid(const GSHOTPyramid&);
        
        /// Constructs a pyramid from the PointCloud of a Scene.
        /// @param[in] input The PointCloud data
        /// @param[in] pad Amount of horizontal, vertical and depth zero padding (in cells).
        /// @param[in] interval Number of levels per octave in the pyramid.
        /// @note The amount of padding and the interval should be at least 1.
        GSHOTPyramid(const PointCloudPtr input, Eigen::Vector3i pad, int interval = 5,
                     float starting_resolution = 0.05/*0.09/2.0*/, int nbOctave = 2);

        /// Constructs a pyramid from a given point cloud data.
        /// @param[in] input The PointCloud data
        /// @param[in] octave Amount of Octaves in the pyramid (An octave can have muliples subsections/intervals)
        /// @param[in] interval Number of intervals in each octave/Nuber of levels per octave in the pyramid
        /// @param[in] starting_resolution Starting resolution for the point cloud. So that we can pre sample the point cloud,
        /// in order to reduce computation and define a standard.
        /// @param[in] starting_kp_grid_reso Resolution keypoint
        /// @note TODO Need to take a container of parameters instead of the descr_rad to generalize to different descriptors.
        GSHOTPyramid(PointCloudPtr input,  int octaves, int interval,
                     float starting_resolution, float starting_kp_grid_reso, float descr_rad);
        
        // Destructor
//        ~GSHOTPyramid();

        /** GETTERS AND SETTER **/
        
        /// Returns whether the pyramid is empty. An empty pyramid has no level.
        bool empty() const;

        Eigen::Vector3i pad() const;
        
        /// Returns the number of levels per octave in the pyramid.
        int interval() const;
        
        /// Returns the pyramid levels.
        /// @note Scales are given by the following formula: 2^(1 - @c index / @c interval).
        const std::vector<Level> & levels() const;

        const std::vector<float> & resolutions() const;
        
        /** OTHERS **/
    
        
        /// Returns the convolutions of the pyramid with a filter.
        /// @param[in] filter Filter.
        /// @param[out] convolutions Convolution of each level.
        void convolve(const Level & filter, vector<Tensor3DF >& convolutions) const;

        void sumConvolve(const Level & filter, vector<Tensor3DF >& convolutions) const;

        /// Maps a const pyramid level to a simple const matrix (useful to apply standard matrix
        /// operations to it).
        /// @note The size of the matrix will be rows x (cols * NbFeatures).
        /// Should be moved in another class
        static Tensor3DF TensorMap(Level level);
        
        /// Return the mean resolution of the cloud
        static double computeCloudResolution (PointCloudConstPtr cloud);
        
//        private:
        
        std::vector<float> minMaxScaler(std::vector<float> data);

        // Method for computing feature spaces. May have different implementations depending on the descriptor used.
        DescriptorsPtr
        compute_descriptor(PointCloudPtr input, PointCloudPtr keypoints, float);
        
        // Method creating the keypoint grid using the min/max values of the input
        /*static */PointCloudPtr
        compute_keypoints(float grid_reso, PointType min, PointType max, int index);
        
        // Computes the 2D convolution of a pyramid level with a filter
        static void Convolve(const Level & level, const Level & filter, Tensor3DF & convolution);
        
//        // Number of keypoints per dimension (needed for the sliding box process)
        std::vector<Eigen::Vector3i, Eigen::aligned_allocator<Eigen::Vector3i> > topology;//number of points at [lvl]

        Eigen::Vector3i pad_;
        int interval_;
        // Represent a vector of 3D scene of descriptors computed at different resolution
        //from 0 (original resolution) to n (lowest resolution, last octave)
        std::vector<Level> levels_;

        std::vector<float> resolutions_;

        //TODO: Can be removed
        // The corresponding positions of the descriptors in the space for each level
        std::vector<PointCloudPtr > keypoints_;
    };

    //Read point cloud from a path
    int readPointCloud(std::string object_path, PointCloudPtr point_cloud);
    
    /// Serializes a pyramid to a stream.
    std::ostream & operator<<(std::ostream & os, const GSHOTPyramid & pyramid);
    
    /// Unserializes a pyramid from a stream.
    std::istream & operator>>(std::istream & is, GSHOTPyramid & pyramid);
}

#endif

