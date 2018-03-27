//--------------------------------------------------------------------------------------------------
// Implementation of the papers "Exact Acceleration of Linear Object Detectors", 12th European
// Conference on Computer Vision, 2012 and "Deformable Part Models with Individual Part Scaling",
// 24th British Machine Vision Conference, 2013.
//
// Copyright (c) 2013 Idiap Research Institute, <http://www.idiap.ch/>
// Written by Charles Dubout <charles.dubout@idiap.ch>
//
// This file is part of FFLDv2 (the Fast Fourier Linear Detector version 2)
//
// FFLDv2 is free software: you can redistribute it and/or modify it under the terms of the GNU
// Affero General Public License version 3 as published by the Free Software Foundation.
//
// FFLDv2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
// General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with FFLDv2. If
// not, see <http://www.gnu.org/licenses/>.
//--------------------------------------------------------------------------------------------------

#include "Intersector.h"
#include "LBFGS.h"
#include "Mixture.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

using namespace Eigen;
using namespace FFLD;
using namespace std;

Mixture::Mixture() : cached_(false), zero_(true)
{
}

Mixture::Mixture(const vector<Model> & models) : models_(models), cached_(false), zero_(true)
{
    cout<<"Mix::contructor zero_ is true"<<endl;
    for(int i=0;i<models_.size(); ++i){
        for(int j=0;j<models_[i].parts().size(); ++j){
            cout<<"Mix::contructor models_["<<i<<"].parts_["<<j<<"].filter.size() : "<< models_[i].parts()[j].filter.size() <<endl;
        }
    }

}

Mixture::Mixture(int nbComponents, const vector<Scene> & scenes, Object::Name name) :
cached_(false), zero_(true)
{
	// Create an empty mixture if any of the given parameters is invalid
	if ((nbComponents <= 0) || scenes.empty()) {
		cerr << "Attempting to create an empty mixture" << endl;
		return;
	}
	
	// Compute the root filters' sizes using Felzenszwalb's heuristic
    const vector<Model::triple<int, int, int> > sizes = FilterSizes(nbComponents, scenes, name);
	
	// Early return in case the root filters' sizes could not be determined
    if (sizes.size() != nbComponents){
        cout<<"root filters sizes could not be determined"<<endl;
        return;
    }
	
	// Initialize the models (with symmetry) to those sizes
    models_.resize(2 * nbComponents);
	
	for (int i = 0; i < nbComponents; ++i) {
		models_[2 * i    ] = Model(sizes[i]);
		models_[2 * i + 1] = Model(sizes[i]);
	}
    cout<<"models2_ size = "<<models_.size()<<endl;
}

bool Mixture::empty() const
{
	return models_.empty();
}

const vector<Model> & Mixture::models() const
{
	return models_;
}

vector<Model> & Mixture::models()
{
	return models_;
}

Model::triple<int, int, int> Mixture::minSize() const
{
    Model::triple<int, int, int> size(0, 0, 0);
	
	if (!models_.empty()) {
		size = models_[0].rootSize();
		
		for (int i = 1; i < models_.size(); ++i) {
			size.first = min(size.first, models_[i].rootSize().first);
			size.second = min(size.second, models_[i].rootSize().second);
            size.third = min(size.third, models_[i].rootSize().third);
		}
	}
	
	return size;
}

Model::triple<int, int, int> Mixture::maxSize() const
{
    Model::triple<int, int, int> size(0, 0, 0);
	
	if (!models_.empty()) {
		size = models_[0].rootSize();
		
		for (int i = 1; i < models_.size(); ++i) {
			size.first = max(size.first, models_[i].rootSize().first);
			size.second = max(size.second, models_[i].rootSize().second);
            size.third = max(size.third, models_[i].rootSize().third);
		}
	}
	
	return size;
}

double Mixture::train(const vector<Scene> & scenes, Object::Name name, Eigen::Vector3i pad,
					  int interval, int nbRelabel, int nbDatamine, int maxNegatives, double C,
					  double J, double overlap)
{
    if (empty() || scenes.empty() || (pad.x() < 1) || (pad.y() < 1) || (pad.z() < 1) || (interval < 1) ||
		(nbRelabel < 1) || (nbDatamine < 1) || (maxNegatives < models_.size()) || (C <= 0.0) ||
		(J <= 0.0) || (overlap <= 0.0) || (overlap >= 1.0)) {
		cerr << "Invalid training parameters" << endl;
		return numeric_limits<double>::quiet_NaN();
	}
	
	// Test if the models are really zero by looking at the first cell of the first filter of the
	// first model
	if (!models_[0].empty() && models_[0].parts()[0].filter.size() &&
        !models_[0].parts()[0].filter()(0, 0, 0).isZero()){
        zero_ = false;
        cout << "Mix:: set zero_ to false" << endl;
    }

	double loss = numeric_limits<double>::infinity();
	
	for (int relabel = 0; relabel < nbRelabel; ++relabel) {
        cout<<"Mix::train relabel : "<< relabel <<endl;

//        for(int i=0;i<models_.size(); ++i){
//            for(int j=0;j<models_[i].parts().size(); ++j){
//                cout<<"Mix::train loop begin models_["<<i<<"].parts_["<<j<<"].filter.size() : "<< models_[i].parts()[j].filter.size() <<endl;
//            }
//        }

		// Sample all the positives
		vector<pair<Model, int> > positives;
		
        posLatentSearch(scenes, name, pad, interval, overlap, positives);

        if(positives.size() > 0) cout << "Mix::train positives isZero = " << GSHOTPyramid::TensorMap(positives[0].first.parts()[0].filter).isZero() << endl;

        // Left-right clustering at the first iteration
        if (zero_)
            Cluster(static_cast<int>(models_.size()), positives);

        // Cache of hard negative samples of maximum size maxNegatives
        vector<pair<Model, int> > negatives;
		
        // Previous loss on the cache
        double prevLoss = -numeric_limits<double>::infinity();
		
        for (int datamine = 0; datamine < nbDatamine; ++datamine) {
            // Remove easy samples (keep hard ones)
            int j = 0;
			
            for (int i = 0; i < negatives.size(); ++i)
                if ((negatives[i].first.parts()[0].deformation(3) =
                     models_[negatives[i].second].dot(negatives[i].first)) > -1.01)
                    negatives[j++] = negatives[i];
			
            negatives.resize(j);

            // Sample new hard negatives
            negLatentSearch(scenes, name, pad, interval, maxNegatives, negatives);

            // Stop if there are no new hard negatives
            if (datamine && (negatives.size() == j))
                break;
			
//            // Merge the left / right samples for more efficient training
//            vector<int> posComponents(positives.size());
			
//            for (int i = 0; i < positives.size(); ++i) {
//                posComponents[i] = positives[i].second;
				
//                // if positives[i].second is impair flip the model
//                if (positives[i].second & 1)
//                    positives[i].first = positives[i].first.flip();
//                // positives[i].second = 0,0,1,1,2,...
//                positives[i].second >>= 1;
//            }
			
//            vector<int> negComponents(negatives.size());
			
//            for (int i = 0; i < negatives.size(); ++i) {
//                negComponents[i] = negatives[i].second;
				
//                if (negatives[i].second & 1)
//                    negatives[i].first = negatives[i].first.flip();
				
//                negatives[i].second >>= 1;
//            }
			
//            for(int i=0;i<models_.size(); ++i){
//                for(int j=0;j<models_[i].parts().size(); ++j){
//                    cout<<"Mix::train loop mid1 models_["<<i<<"].parts_["<<j<<"].filter.size() : "<< models_[i].parts()[j].filter.size() <<endl;
//                }
//            }

//            // Merge the left / right models for more efficient training
//            for (int i = 1; i < models_.size() / 2; ++i)
//                models_[i] = models_[i * 2];
			
//            models_.resize(models_.size() / 2);
			
//            for(int i=0;i<models_.size(); ++i){
//                for(int j=0;j<models_[i].parts().size(); ++j){
//                    cout<<"Mix::train loop mid2 models_["<<i<<"].parts_["<<j<<"].filter.size() : "<< models_[i].parts()[j].filter.size() <<endl;
//                }
//            }

            const int maxIterations =
                min(max(10.0 * sqrt(static_cast<double>(positives.size())), 100.0), 1000.0);

            loss = train(positives, negatives, C, J, maxIterations);

            cout << "Relabel: " << relabel << ", datamine: " << datamine
                 << ", # positives: " << positives.size() << ", # hard negatives: " << j
                 << " (already in the cache) + " << (negatives.size() - j) << " (new) = "
                 << negatives.size() << ", loss (cache): " << loss << endl;
			
//            // Unmerge the left / right samples
//            for (int i = 0; i < positives.size(); ++i) {
//                positives[i].second = posComponents[i];
				
//                if (positives[i].second & 1)
//                    positives[i].first = positives[i].first.flip();
//            }
			
//            for (int i = 0; i < negatives.size(); ++i) {
//                negatives[i].second = negComponents[i];
				
//                if (negatives[i].second & 1)
//                    negatives[i].first = negatives[i].first.flip();
//            }
			
//            // Unmerge the left / right models
//            models_.resize(models_.size() * 2);
			
//            for (int i = static_cast<int>(models_.size()) / 2 - 1; i >= 0; --i) {
//                models_[i * 2    ] = models_[i];
//                models_[i * 2 + 1] = models_[i].flip();
//            }
			
            // The filters definitely changed
//            filterCache_.clear();
            cached_ = false;
            zero_ = false;
            cout << "Mix:: set cached_ to false" << endl;
            cout << "Mix:: set zero_ to false" << endl;

			
            // Save the latest model so as to be able to look at it while training
            ofstream out("tmp.txt");
			
            out << (*this);
			
            // Stop if we are not making progress
            if ((0.999 * loss < prevLoss) && (negatives.size() < maxNegatives))
                break;
			
            prevLoss = loss;
        }

//        for(int i=0;i<models_.size(); ++i){
//            for(int j=0;j<models_[i].parts().size(); ++j){
//                cout<<"Mix::train loop end models_["<<i<<"].parts_["<<j<<"].filter.size() : "<< models_[i].parts()[j].filter.size() <<endl;
//            }
//        }
	}
	
	return loss;
}

void Mixture::initializeParts(int nbParts, Model::triple<int, int, int> partSize, GSHOTPyramid::Level root2x)
{
	for (int i = 0; i < models_.size(); i += 2) {
        models_[i].initializeParts(nbParts, partSize, root2x);
		models_[i + 1] = models_[i].flip();
	}
	
	// The filters definitely changed
	filterCache_.clear();
	cached_ = false;
	zero_ = false;
}

void Mixture::computeEnergyScores(const GSHOTPyramid & pyramid, vector<Tensor3DF> & scores,
                                  vector<Indices> & argmaxes,
                                  vector<vector<vector<Model::Positions> > > * positions) const
{

    const int nbModels = static_cast<int>(models_.size());
    const int nbLevels = static_cast<int>(pyramid.levels().size());

    // Resize the scores and argmaxes
    scores.resize(nbLevels);
//    cout<<"computeEnergyScores:: scores size : "<< scores.size()<<endl;
    argmaxes.resize(nbLevels);

    if (empty() || pyramid.empty()) {
        if(empty())
            cout << "computeEnergyScores::mixture models are empty" << empty() << endl;
        else
            cout << "computeEnergyScores::pyramid.empty()" << endl;
        scores.clear();
        argmaxes.clear();

        if (positions)
            positions->clear();

        return;
    }

    // Convolve with all the models
    vector<vector<Tensor3DF> > convolutions;
//    cout<<"Mix::computeEnergyScores::convolve ..."<<endl;
    convolve(pyramid, convolutions, positions);
//    cout<<"Mix::computeEnergyScores::convolve done, convolutions.size : "<< convolutions.size() << " / " << convolutions[0].size()<<endl;

    // In case of error
    if (convolutions.empty()) {
        cout << "Mix::computeEnergyScores::convolutions.empty()" << endl;
        scores.clear();
        argmaxes.clear();

//        if (positions)
//            positions->clear();

        return;
    }


//#pragma omp parallel for
    for (int lvl = 0; lvl < nbLevels; ++lvl) {
        int rows = static_cast<int>(convolutions[0][lvl].rows());
        int cols = static_cast<int>(convolutions[0][lvl].cols());
        int depths = static_cast<int>(convolutions[0][lvl].depths());


        for (int i = 1; i < nbModels; ++i) {
//            cout<<"Mix::computeEnergyScores:: for model : "<< i << " on level :" << lvl <<endl;
//            cout<<"Mix::computeEnergyScores::convolutions[i][lvl] : "<< convolutions[i][lvl].size() <<endl;
            rows = std::min(rows, static_cast<int>(convolutions[i][lvl].rows()));
            cols = std::min(cols, static_cast<int>(convolutions[i][lvl].cols()));
            depths = std::min(depths, static_cast<int>(convolutions[i][lvl].depths()));
        }

//        cout<<"computeEnergyScores::depths : "<< depths <<endl;
//        cout<<"computeEnergyScores::rows : "<< rows <<endl;
//        cout<<"computeEnergyScores::cols : "<< cols <<endl;
        scores[lvl]().resize(depths, rows, cols);
        argmaxes[lvl]().resize(depths, rows, cols);
//        cout<<"computeEnergyScores::lvl : "<< lvl <<endl;

        for (int z = 0; z < depths; ++z) {
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    int argmax = 0;

                    for (int i = 1; i < nbModels; ++i){
                        if (convolutions[i][lvl]()(z, y, x) > convolutions[argmax][lvl]()(z, y, x))
                            argmax = i;

//                        cout << "Mix::computeEnergyScores argmax = " << argmax << endl;
//                        cout << "Mix::computeEnergyScores convolutions["<<i<<"]["<<lvl<<"]()(z, y, x) = " << convolutions[i][lvl]()(z, y, x) << endl;
//                        cout << "Mix::computeEnergyScores convolutions["<<argmax<<"]["<<lvl<<"]()(z, y, x) = " << convolutions[argmax][lvl]()(z, y, x) << endl;
                    }
                    scores[lvl]()(z, y, x) = convolutions[argmax][lvl]()(z, y, x);
                    argmaxes[lvl]()(z, y, x) = argmax;
                }
            }
        }
        cout << "Mix::computeEnergyScores scores[lvl] is zero = ["<<lvl<<"] : "
             <<scores[lvl].isZero() << endl;
    }
}

//void Mixture::convolve(const HOGPyramid & pyramid, vector<HOGPyramid::Matrix> & scores,
//					   vector<Indices> & argmaxes,
//					   vector<vector<vector<Model::Positions> > > * positions) const
//{
//	if (empty() || pyramid.empty()) {
//		scores.clear();
//		argmaxes.clear();
		
//		if (positions)
//			positions->clear();
		
//		return;
//	}
	
//	const int nbModels = static_cast<int>(models_.size());
//	const int nbLevels = static_cast<int>(pyramid.levels().size());
	
//	// Convolve with all the models
//	vector<vector<HOGPyramid::Matrix> > convolutions;
	
//	convolve(pyramid, convolutions, positions);
	
//	// In case of error
//	if (convolutions.empty()) {
//		scores.clear();
//		argmaxes.clear();
		
//		if (positions)
//			positions->clear();
		
//		return;
//	}
	
//	// Resize the scores and argmaxes
//	scores.resize(nbLevels);
//	argmaxes.resize(nbLevels);
	
//#pragma omp parallel for
//	for (int z = 0; z < nbLevels; ++z) {
//		int rows = static_cast<int>(convolutions[0][z].rows());
//		int cols = static_cast<int>(convolutions[0][z].cols());
		
//		for (int i = 1; i < nbModels; ++i) {
//			rows = min(rows, static_cast<int>(convolutions[i][z].rows()));
//			cols = min(cols, static_cast<int>(convolutions[i][z].cols()));
//		}
		
//		scores[z].resize(rows, cols);
//		argmaxes[z].resize(rows, cols);
		
//		for (int y = 0; y < rows; ++y) {
//			for (int x = 0; x < cols; ++x) {
//				int argmax = 0;
				
//				for (int i = 1; i < nbModels; ++i)
//					if (convolutions[i][z](y, x) > convolutions[argmax][z](y, x))
//						argmax = i;
				
//				scores[z](y, x) = convolutions[argmax][z](y, x);
//				argmaxes[z](y, x) = argmax;
//			}
//		}
//	}
//}

void Mixture::cacheFilters() const
{
//	// Count the number of filters
//	int nbFilters = 0;
	
//	for (int i = 0; i < models_.size(); ++i)
//		nbFilters += models_[i].parts().size();
	
//	// Transform all the filters
//	filterCache_.resize(nbFilters);
	
//	for (int i = 0, j = 0; i < models_.size(); ++i) {
//#pragma omp parallel for
//		for (int k = 0; k < models_[i].parts().size(); ++k)
//			Patchwork::TransformFilter(models_[i].parts()[k].filter, filterCache_[j + k]);
		
//		j += models_[i].parts().size();
//	}
	
//	cached_ = true;
}

static inline void clipBndBox(Rectangle & bndbox, const Scene & scene, double alpha = 0.0)
{
    //TODO COMPRENDRE A QUOI CA SERT
	// Compromise between clamping the bounding box to the image and penalizing bounding boxes
	// extending outside the image
//    if (bndbox.left() < 0)
//        bndbox.setLeft(bndbox.left() * alpha - 0.5);

//    if (bndbox.top() < 0)
//        bndbox.setTop(bndbox.top() * alpha - 0.5);

//    if (bndbox.front() < 0)
//        bndbox.setFront(bndbox.front() * alpha - 0.5);

//    if (bndbox.right() >= scene.width())
//        bndbox.setRight(scene.width() - 1 + (bndbox.right() - scene.width() + 1) * alpha + 0.5);

//    if (bndbox.bottom() >= scene.height())
//        bndbox.setBottom(scene.height() - 1 + (bndbox.bottom() - scene.height() + 1) * alpha + 0.5);

//    if (bndbox.back() >= scene.depth())
//        bndbox.setBack(scene.depth() - 1 + (bndbox.back() - scene.depth() + 1) * alpha + 0.5);
}

void Mixture::posLatentSearch(const vector<Scene> & scenes, Object::Name name, Eigen::Vector3i pad,
							  int interval, double overlap,
							  vector<pair<Model, int> > & positives) const
{
    cout << "Mix::posLatentSearch ..." << endl;
    if (scenes.empty() || (pad.x() < 1) || (pad.y() < 1) || (pad.z() < 1) || (interval < 1) || (overlap <= 0.0) ||
		(overlap >= 1.0)) {
		positives.clear();
		cerr << "Invalid training paramters" << endl;
		return;
	}
	
	positives.clear();
	
	for (int i = 0; i < scenes.size(); ++i) {
		// Skip negative scenes
		bool negative = true;
		
		for (int j = 0; j < scenes[i].objects().size(); ++j)
			if ((scenes[i].objects()[j].name() == name) && !scenes[i].objects()[j].difficult())
				negative = false;
		
		if (negative)
			continue;
		
        PointCloudPtr cloud( new PointCloudT);
		
        if (pcl::io::loadPCDFile<PointType>(scenes[i].filename().c_str(), *cloud) == -1) {
            cout<<"couldnt open pcd file"<<endl;
			positives.clear();
			return;
		}
		
        const GSHOTPyramid pyramid(cloud, pad, interval);
		
        cout << "Mix::posLatentSearch create pyramid of " << pyramid.levels().size() << " levels" << endl;


		if (pyramid.empty()) {
            cout<<"posLatentSearch::pyramid.empty"<<endl;
			positives.clear();
			return;
		}
		
        vector<Tensor3DF> scores;
		vector<Indices> argmaxes;
        vector<vector<vector<Model::Positions> > > positions;//positions[nbModels][nbLvl][nbPart]
		
        if (!zero_){
//            cout << "pos computeEnergyScores ..." << endl;
            computeEnergyScores(pyramid, scores, argmaxes, &positions);

            cout << "Mix::computeEnergyScores scores["<<0<<"] is Zero = " << scores[0].isZero() << endl;
        }

        // For each object, set as positive the best (highest score or else most intersecting)
        // position
        for (int j = 0; j < scenes[i].objects().size(); ++j) {
            // Ignore objects with a different name or difficult objects
            if ((scenes[i].objects()[j].name() != name) || scenes[i].objects()[j].difficult())
                continue;
			
            const Intersector intersector(scenes[i].objects()[j].bndbox(), overlap);
			
            // The model, level, position, score, and intersection of the best example
            int argModel = -1;
            int argX = -1;
            int argY = -1;
            int argZ = -1;
            int argLvl =-1;
            double maxScore = -numeric_limits<double>::infinity();
            double maxInter = 0.0;
			
            for (int lvl = 0; lvl < pyramid.levels().size(); ++lvl) {

                const double scale = 1;//0.05 * pow(2.0, static_cast<double>(lvl) / interval /*+ 2*/);
                cout << "Mix::posLatentSearch scale : " << scale << endl;
                int rows = 0;
                int cols = 0;
                int depths = 0;
				
                if (!zero_) {
                    cout << "Mix::posLatentSearch scores["<<lvl<<"].size() = " << scores[lvl].size() << endl;

                    depths = scores[lvl].depths();
                    rows = scores[lvl].rows();
                    cols = scores[lvl].cols();
                    cout << "Mix::posLatentSearch depths = scores["<<lvl<<"].depths() = " << depths << endl;
                    cout << "Mix::posLatentSearch rows = scores["<<lvl<<"].rows() = " << rows << endl;
                    cout << "Mix::posLatentSearch cols = scores["<<lvl<<"].cols() = " << cols << endl;
                }
                else if (lvl >= interval) {
                    depths = static_cast<int>(pyramid.levels()[lvl].depths()) - maxSize().first + 1;
                    rows = static_cast<int>(pyramid.levels()[lvl].rows()) - maxSize().second + 1;
                    cols = static_cast<int>(pyramid.levels()[lvl].cols()) - maxSize().third + 1;
                    cout << "Mix:: depths = pyramid.levels()[lvl].depths()) - maxSize().first + 1 = " << depths << endl;
                    cout << "Mix:: rows = pyramid.levels()[lvl].rows()) - maxSize().second + 1 = " << rows << endl;
                    cout << "Mix:: cols = pyramid.levels()[lvl].cols()) - maxSize().third + 1 = " << cols << endl;
                }
				
                for (int z = 0; z < depths; ++z) {
                    for (int y = 0; y < rows; ++y) {
                        for (int x = 0; x < cols; ++x) {
                            // Find the best matching model (highest score or else most intersecting)
                            int model = zero_ ? 0 : argmaxes[lvl]()(z, y, x);
                            double intersection = 0.0;

                            // Try all models and keep the most intersecting one
                            if (zero_) {
                                for (int k = 0; k < models_.size(); ++k) {
                                    // The bounding box of the model at this position
                                    //TODO

                                    Eigen::Vector3i origin(x - pad.x(), y - pad.y(), z - pad.z());
                                    int w = models_[k].rootSize().third * scale;
                                    int h = models_[k].rootSize().second * scale;
                                    int d = models_[k].rootSize().first * scale;

                                    Rectangle bndbox( origin * scale, w, h, d);//indices of the cube in the PC
                                    //bndbox.setX( topology[ min(0, x - pad.x())].x)


                                    // Trade-off between clipping and penalizing
                                    clipBndBox(bndbox, scenes[i], 0.5);

                                    double inter = 0.0;

                                    if (intersector(bndbox, &inter)) {
                                        cout << "Mix::posLatentSearch intersector score : " << inter << " / " <<  intersection << endl;
                                        if (inter > intersection) {
                                            cout << "Mix::posLatentSearch intersector true" << endl;
                                            model = k;
                                            intersection = inter;
                                        }
                                    }
                                }
                            }
                            // Just take the model with the best score
                            else {
                                //TODO
                                // The bounding box of the model at this position
                                Eigen::Vector3i origin(x - pad.x(), y - pad.y(), z - pad.z());
                                int w = models_[model].rootSize().third * scale;
                                int h = models_[model].rootSize().second * scale;
                                int d = models_[model].rootSize().first * scale;

                                Rectangle bndbox( origin * scale, w, h, d);//indices of the cube in the PC


                                clipBndBox(bndbox, scenes[i]);
                                intersector(bndbox, &intersection);

//                                cout << "Mix::posLatentSearch intersector bndbox : " << scenes[i].objects()[j].bndbox() << endl;
//                                cout << "Mix::posLatentSearch rectangle bndbox : " << bndbox << endl;
//                                cout << "Mix::posLatentSearch intersector score / maxInter : " << intersection << " / " <<  maxInter << endl;
                            }


                            if ((intersection > maxInter) && (zero_ || (scores[lvl]()(z, y, x) > maxScore))) {
                                argModel = model;
                                argX = x;
                                argY = y;
                                argZ = z;
                                argLvl = lvl;

                                if (!zero_)
                                    maxScore = scores[lvl]()(z, y, x);

                                maxInter = intersection;
                            }
                        }
                    }
                }
            }
            cout << "maxInter : " << maxInter << " >= overlap :" << overlap << endl;

            if (maxInter >= overlap) {
                cout << "Mix:PosLatentSearch found a positive sample" << endl;

                Model sample;
				
                models_[argModel].initializeSample(pyramid, argX, argY, argZ, argLvl, sample,
                                                   zero_ ? 0 : &positions[argModel]);
				
                if (!sample.empty())
                    positives.push_back(make_pair(sample, argModel));
            }
        }
    }
    cout << "pos latent search done" << endl;
}

static inline bool operator==(const Model & a, const Model & b)
{
	return (a.parts()[0].offset == b.parts()[0].offset) &&
		   (a.parts()[0].deformation(0) == b.parts()[0].deformation(0)) &&
		   (a.parts()[0].deformation(1) == b.parts()[0].deformation(1));
}

static inline bool operator<(const Model & a, const Model & b)
{
	return (a.parts()[0].offset(0) < b.parts()[0].offset(0)) ||
		   ((a.parts()[0].offset(0) == b.parts()[0].offset(0)) &&
			((a.parts()[0].offset(1) < b.parts()[0].offset(1)) ||
			 ((a.parts()[0].offset(1) == b.parts()[0].offset(1)) &&
			  ((a.parts()[0].deformation(0) < b.parts()[0].deformation(0)) ||
			   ((a.parts()[0].deformation(0) == b.parts()[0].deformation(0)) &&
			    ((a.parts()[0].deformation(1) < b.parts()[0].deformation(1))))))));
}

void Mixture::negLatentSearch(const vector<Scene> & scenes, Object::Name name, Eigen::Vector3i pad,
                              int interval, int maxNegatives,
                              vector<pair<Model, int> > & negatives) const
{
    // Sample at most (maxNegatives - negatives.size()) negatives with a score above -1.0
    if (scenes.empty() || (pad.x() < 1) || (pad.y() < 1) || (pad.z() < 1) || (interval < 1) || (maxNegatives <= 0) ||
            (negatives.size() >= maxNegatives)) {
        negatives.clear();
        cerr << "Invalid training paramters" << endl;
        return;
    }

    // The number of negatives already in the cache
    const int nbCached = static_cast<int>(negatives.size());

    for (int i = 0, j = 0; i < scenes.size(); ++i) {
        // Skip positive scenes
        bool positive = false;

        for (int k = 0; k < scenes[i].objects().size(); ++k)
            if (scenes[i].objects()[k].name() == name)
                positive = true;

        if (positive)
            continue;

        PointCloudPtr cloud( new PointCloudT);

        if (pcl::io::loadPCDFile<PointType>(scenes[i].filename().c_str(), *cloud) == -1) {
            negatives.clear();
            return;
        }

        const GSHOTPyramid pyramid(cloud, pad, interval);

        if (pyramid.empty()) {
            negatives.clear();
            return;
        }

        vector<Tensor3DF> scores;
        vector<Indices> argmaxes;
        vector<vector<vector<Model::Positions> > > positions;

        if (!zero_){
            cout << "Mix::NegLatentSearch computeEnergyScores ..." << endl;
            computeEnergyScores(pyramid, scores, argmaxes, &positions);
        }

        for (int lvl = 0; lvl < pyramid.levels().size(); ++lvl) {
            int rows = 0;
            int cols = 0;
            int depths = 0;

            if (!zero_) {
                rows = static_cast<int>(scores[lvl].rows());
                cols = static_cast<int>(scores[lvl].cols());
                depths = static_cast<int>(scores[lvl].depths());
            }
            else if (lvl >= interval) {
                depths = static_cast<int>(pyramid.levels()[lvl].depths()) - maxSize().first + 1;
                rows = static_cast<int>(pyramid.levels()[lvl].rows()) - maxSize().second + 1;
                cols = static_cast<int>(pyramid.levels()[lvl].cols()) - maxSize().third + 1;
            }

            for (int z = 0; z < depths; ++z) {
                for (int y = 0; y < rows; ++y) {
                    for (int x = 0; x < cols; ++x) {

                        const int argmax = zero_ ? (rand() % models_.size()) : argmaxes[lvl]()(z, y, x);

                        if (zero_ || (scores[lvl]()(z, y, x) > -1)) {
                            Model sample;

                            models_[argmax].initializeSample(pyramid, x, y, z, lvl, sample,
                                                             zero_ ? 0 : &positions[argmax]);
                            if (!sample.empty()) {
                                // Store all the information about the sample in the offset and
                                // deformation of its root
                                sample.parts()[0].offset(0) = i;
                                sample.parts()[0].offset(1) = lvl;
                                sample.parts()[0].deformation(0) = z;
                                sample.parts()[0].deformation(1) = y;
                                sample.parts()[0].deformation(2) = x;
                                sample.parts()[0].deformation(3) = argmax;
                                sample.parts()[0].deformation(4) = zero_ ? 0.0 : scores[lvl]()(z, y, x);

                                // Look if the same sample was already sampled
                                while ((j < nbCached) && (negatives[j].first < sample))
                                    ++j;

                                // Make sure not to put the same sample twice
                                if ((j >= nbCached) || !(negatives[j].first == sample)) {
                                    negatives.push_back(make_pair(sample, argmax));

                                    if (negatives.size() == maxNegatives)
                                        return;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

namespace FFLD
{
namespace detail
{
class Loss : public LBFGS::IFunction
{
public:
	Loss(vector<Model> & models, const vector<pair<Model, int> > & positives,
		 const vector<pair<Model, int> > & negatives, double C, double J, int maxIterations) :
	models_(models), positives_(positives), negatives_(negatives), C_(C), J_(J),
	maxIterations_(maxIterations)
	{
	}
	
	virtual int dim() const
	{
		int d = 0;
		
		for (int i = 0; i < models_.size(); ++i) {
			for (int j = 0; j < models_[i].parts().size(); ++j) {
                d += models_[i].parts()[j].filter.size() * GSHOTPyramid::DescriptorSize; // Filter
				
				if (j)
                    d += 8; // Deformation
			}
			
			++d; // Bias
		}
		
		return d;
	}
	
	virtual double operator()(const double * x, double * g = 0) const
	{
        std::cout << "LOSS::() ..." << std::endl;
		// Recopy the features into the models
		ToModels(x, models_);
		
		// Compute the loss and gradient over the samples
		double loss = 0.0;
		
		vector<Model> gradients;
		
		if (g) {
			gradients.resize(models_.size());
			
			for (int i = 0; i < models_.size(); ++i)
				gradients[i] = Model(models_[i].rootSize(),
									 static_cast<int>(models_[i].parts().size()) - 1,
									 models_[i].partSize());
		}
        std::cout << "LOSS::() posMargins ... size : " << positives_.size() << std::endl;
        if(positives_.size()>0) std::cout << "LOSS::() positives_[0].second : " << positives_[0].second << std::endl;
		vector<double> posMargins(positives_.size());
		
#pragma omp parallel for
		for (int i = 0; i < positives_.size(); ++i)
			posMargins[i] = models_[positives_[i].second].dot(positives_[i].first);
		
		for (int i = 0; i < positives_.size(); ++i) {
			if (posMargins[i] < 1.0) {
				loss += 1.0 - posMargins[i];
				
				if (g)
					gradients[positives_[i].second] -= positives_[i].first;
			}
		}
		
        std::cout << "LOSS::() Reweight the positives ..." << std::endl;

        // Reweight thpositives
		if (J_ != 1.0) {
			loss *= J_;
			
			if (g) {
				for (int i = 0; i < models_.size(); ++i)
					gradients[i] *= J_;
			}
		}
        std::cout << "LOSS::() negMargins ..." << std::endl;

		vector<double> negMargins(negatives_.size());
		
#pragma omp parallel for
		for (int i = 0; i < negatives_.size(); ++i)
			negMargins[i] = models_[negatives_[i].second].dot(negatives_[i].first);
		
		for (int i = 0; i < negatives_.size(); ++i) {
			if (negMargins[i] > -1.0) {
				loss += 1.0 + negMargins[i];
				
				if (g)
					gradients[negatives_[i].second] += negatives_[i].first;
			}
		}
        std::cout << "LOSS::() test1" << std::endl;

		// Add the loss and gradient of the regularization term
		double maxNorm = 0.0;
		int argNorm = 0;
		
		for (int i = 0; i < models_.size(); ++i) {
			if (g)
				gradients[i] *= C_;
            std::cout << "LOSS::() model norm()..." << std::endl;

			const double norm = models_[i].norm();
            std::cout << "LOSS::() norm = "<<norm<<" / maxNorm = "<<maxNorm << std::endl;

			if (norm > maxNorm) {
				maxNorm = norm;
				argNorm = i;
			}
		}
        std::cout << "LOSS::() test2" << std::endl;

		// Recopy the gradient if needed
		if (g) {
			// Regularization gradient
			gradients[argNorm] += models_[argNorm];
			
			// Regularize the deformation 10 times more
			for (int i = 1; i < gradients[argNorm].parts().size(); ++i)
				gradients[argNorm].parts()[i].deformation +=
					9.0 * models_[argNorm].parts()[i].deformation;
			
			// Do not regularize the bias
			gradients[argNorm].bias() -= models_[argNorm].bias();
            std::cout << "LOSS::() test3" << std::endl;

			// In case minimum constraints were applied
			for (int i = 0; i < models_.size(); ++i) {
				for (int j = 1; j < models_[i].parts().size(); ++j) {
					if (models_[i].parts()[j].deformation(0) >= -0.005)
						gradients[i].parts()[j].deformation(0) =
							max(gradients[i].parts()[j].deformation(0), 0.0);
					
					if (models_[i].parts()[j].deformation(2) >= -0.005)
						gradients[i].parts()[j].deformation(2) =
							max(gradients[i].parts()[j].deformation(2), 0.0);
					
					if (models_[i].parts()[j].deformation(4) >= -0.005)
						gradients[i].parts()[j].deformation(4) =
							max(gradients[i].parts()[j].deformation(4), 0.0);

                    if (models_[i].parts()[j].deformation(6) >= -0.005)
                        gradients[i].parts()[j].deformation(6) =
                            max(gradients[i].parts()[j].deformation(6), 0.0);
				}
			}
            std::cout << "LOSS::() test4" << std::endl;

			FromModels(gradients, g);
		}
		
		return 0.5 * maxNorm * maxNorm + C_ * loss;
	}
	
	static void ToModels(const double * x, vector<Model> & models)
	{
        std::cout << "LOSS::ToModels ..." << std::endl;
		for (int i = 0, j = 0; i < models.size(); ++i) {
			for (int k = 0; k < models[i].parts().size(); ++k) {
				const int nbFeatures = static_cast<int>(models[i].parts()[k].filter.size()) *
                                       GSHOTPyramid::DescriptorSize;
				
                copy(x + j, x + j + nbFeatures, models[i].parts()[k].filter().data()->data());
				
				j += nbFeatures;
				
				if (k) {
					// Apply minimum constraints
                    models[i].parts()[k].deformation(0) = std::min((x + j)[0],-0.005);
					models[i].parts()[k].deformation(1) = (x + j)[1];
                    models[i].parts()[k].deformation(2) = std::min((x + j)[2],-0.005);
					models[i].parts()[k].deformation(3) = (x + j)[3];
                    models[i].parts()[k].deformation(4) = std::min((x + j)[4],-0.005);
					models[i].parts()[k].deformation(5) = (x + j)[5];
                    models[i].parts()[k].deformation(6) = std::min((x + j)[6],-0.005);
                    models[i].parts()[k].deformation(7) = (x + j)[7];
					
                    j += 8;
				}
			}
			
			models[i].bias() = x[j];
			
			++j;
		}
	}
	
	static void FromModels(const vector<Model> & models, double * x)
	{
		for (int i = 0, j = 0; i < models.size(); ++i) {
			for (int k = 0; k < models[i].parts().size(); ++k) {
				const int nbFeatures = static_cast<int>(models[i].parts()[k].filter.size()) *
                                       GSHOTPyramid::DescriptorSize;
				
                copy(models[i].parts()[k].filter().data()->data(),
                     models[i].parts()[k].filter().data()->data() + nbFeatures, x + j);
				
				j += nbFeatures;
				
				if (k) {
					copy(models[i].parts()[k].deformation.data(),
                         models[i].parts()[k].deformation.data() + 8, x + j);
					
                    j += 8;
				}
			}
			
			x[j] = models[i].bias();
			
			++j;
		}
	}
	
private:
	vector<Model> & models_;
	const vector<pair<Model, int> > & positives_;
	const vector<pair<Model, int> > & negatives_;
	double C_;
	double J_;
	int maxIterations_;
};}
}

double Mixture::train(const vector<pair<Model, int> > & positives,
					  const vector<pair<Model, int> > & negatives, double C, double J,
					  int maxIterations)
{
    cout << "Mix::trainSVM ..." << endl;

	detail::Loss loss(models_, positives, negatives, C, J, maxIterations);
    cout << "Mix::trainSVM loss created" << endl;

	LBFGS lbfgs(&loss, 0.001, maxIterations, 20, 20);
    cout << "Mix::trainSVM lbfgs created" << endl;

	
	// Start from the current models
	VectorXd x(loss.dim());
	
	detail::Loss::FromModels(models_, x.data());
	
	const double l = lbfgs(x.data());
	
	detail::Loss::ToModels(x.data(), models_);
	
	return l;
}

void Mixture::convolve(const GSHOTPyramid & pyramid,
                       vector<vector<Tensor3DF> > & scores,//[model.size][model.part.size]
                       vector<vector<vector<Model::Positions> > > * positions) const//[model.size][model.part.size][pyramid.lvl.size]
{
//    cout<<"Mix::convolve ..."<<endl;

	if (empty() || pyramid.empty()) {
        if (empty()) cout<<"Mix::convolve mixture is empty"<<endl;
        else cout<<"Mix::convolve pyramid is empty"<<endl;

		scores.clear();
		
		if (positions)
			positions->clear();
		
		return;
	}
	
	const int nbModels = static_cast<int>(models_.size());
	
	// Resize the scores and positions
	scores.resize(nbModels);
//    cout<<"Mix::convolve scores.size() : "<< scores.size() <<endl;
	
	if (positions)
		positions->resize(nbModels);
	
	// Transform the filters if needed


//#pragma omp parallel for
    for (int i = 0; i < nbModels; ++i){
//        vector<vector<Tensor3DF> > convolutions(models_[i].parts().size());

        models_[i].convolve(pyramid, scores[i], positions ? &(*positions)[i] : 0/*, &convolutions*/);
//        cout<<"Mix::model["<<i<<"].convolve(pyramid, ...) done"<<endl;
        cout<<"Mix::convolve for each models_ : scores["<<i<<"][0].size = "<<scores[i][0].size()<<endl;

    }
}

//TODO
std::vector<Model::triple<int, int, int> > Mixture::FilterSizes(int nbComponents, const vector<Scene> & scenes,
											 Object::Name name)
{
	// Early return in case the filters or the dataset are empty
	if ((nbComponents <= 0) || scenes.empty())
        return std::vector<Model::triple<int, int, int> >();
	
	// Sort the aspect ratio of all the (non difficult) samples
	vector<double> ratios;
	
	for (int i = 0; i < scenes.size(); ++i) {
		for (int j = 0; j < scenes[i].objects().size(); ++j) {
			const Object & obj = scenes[i].objects()[j];
			
			if ((obj.name() == name) && !obj.difficult())
				ratios.push_back(static_cast<double>(obj.bndbox().width()) / obj.bndbox().height());
		}
	}
	
	// Early return if there is no object
	if (ratios.empty())
        return std::vector<Model::triple<int, int, int> >();
	
	// Sort the aspect ratio of all the samples
	sort(ratios.begin(), ratios.end());
	
	// For each mixture model
	vector<double> references(nbComponents);
	
	for (int i = 0; i < nbComponents; ++i)
		references[i] = ratios[(i * ratios.size()) / nbComponents];
	
	// Store the areas of the objects associated to each component
	vector<vector<int> > areas(nbComponents);
	
	for (int i = 0; i < scenes.size(); ++i) {
		for (int j = 0; j < scenes[i].objects().size(); ++j) {
			const Object & obj = scenes[i].objects()[j];
			
			if ((obj.name() == name) && !obj.difficult()) {
				const double r = static_cast<double>(obj.bndbox().width()) / obj.bndbox().height();
				
				int k = 0;
				
				while ((k + 1 < nbComponents) && (r >= references[k + 1]))
					++k;
				
				areas[k].push_back(obj.bndbox().width() * obj.bndbox().height());
			}
		}
	}
	
	// For each component in reverse order
    std::vector<Model::triple<int, int, int> > sizes(nbComponents);
	
	for (int i = nbComponents - 1; i >= 0; --i) {
		if (!areas[i].empty()) {
			sort(areas[i].begin(), areas[i].end());
			
			const int area = min(max(areas[i][(areas[i].size() * 2) / 10], 3000), 5000);
			const double ratio = ratios[(ratios.size() * (i * 2 + 1)) / (nbComponents * 2)];
			
			sizes[i].first = sqrt(area / ratio) / 8.0 + 0.5;
			sizes[i].second = sqrt(area * ratio) / 8.0 + 0.5;
		}
		else {
			sizes[i] = sizes[i + 1];
		}
	}
	
	return sizes;
}

void Mixture::Cluster(int nbComponents, vector<pair<Model, int> > & samples)
{
	// Early return in case the filters or the dataset are empty
	if ((nbComponents <= 0) || samples.empty())
		return;
	
	// For each model
	for (int i = nbComponents - 1; i >= 0; --i) {
		// Indices of the positives
		vector<int> permutation;
		
		// For each positive
		for (int j = 0; j < samples.size(); ++j)
			if (samples[j].second / 2 == i)
				permutation.push_back(j);
		
		// Next model if this one has no associated positives
		if (permutation.empty())
			continue;
		
		// Score of the best split so far
		double best = 0.0;
		
		// Do 1000 clustering trials
		for (int j = 0; j < 1000; ++j) {
			random_shuffle(permutation.begin(), permutation.end());
			
			vector<bool> assignment(permutation.size(), false);
			Model left = samples[permutation[0]].first;
			
			for (int k = 1; k < permutation.size(); ++k) {
				const Model & positive = samples[permutation[k]].first;
				
				if (positive.dot(left) > positive.dot(left.flip())) {
					left += positive;
				}
				else {
					left += positive.flip();
					assignment[k] = true;
				}
			}
			
			left *= 1.0 / permutation.size();
			
			const Model right = left.flip();
			double dots = 0.0;
			
			for (int k = 0; k < permutation.size(); ++k)
				dots += samples[permutation[k]].first.dot(assignment[k] ? right : left);
			
			if (dots > best) {
				for (int k = 0; k < permutation.size(); ++k)
					samples[permutation[k]].second = 2 * i + assignment[k];
				
				best = dots;
			}
		}
	}
}

ostream & FFLD::operator<<(ostream & os, const Mixture & mixture)
{
	// Save the number of models (mixture components)
	os << mixture.models().size() << endl;
	
	// Save the models themselves
	for (int i = 0; i < mixture.models().size(); ++i)
		os << mixture.models()[i] << endl;
	
	return os;
}

istream & FFLD::operator>>(istream & is, Mixture & mixture)
{
	int nbModels;
	
	is >> nbModels;
	
	if (!is || (nbModels <= 0)) {
		mixture = Mixture();
		return is;
	}
	
	vector<Model> models(nbModels);
	
	for (int i = 0; i < nbModels; ++i) {
		is >> models[i];
		
		if (!is || models[i].empty()) {
			mixture = Mixture();
			return is;
		}
	}
    mixture.models() = models;
	//mixture.models().swap(models);
	
	return is;
}
