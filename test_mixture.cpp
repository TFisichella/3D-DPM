#include "Mixture.h"
#include "Intersector.h"
#include "Object.h"


#include <cstdlib>
#include <sys/timeb.h>

#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/kdtree/impl/kdtree_flann.hpp>
#include <pcl/features/board.h>
#include <pcl/filters/uniform_sampling.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/features/shot_omp.h>
#include <pcl/common/transforms.h>
#include <pcl/common/common_headers.h>


using namespace FFLD;
using namespace std;
using namespace Eigen;

struct Detection : public Rectangle
{
    GSHOTPyramid::Scalar score;
    int x;
    int y;
    int z;
    int lvl;

    Detection() : score(0), x(0), y(0), z(0), lvl(0)
    {
    }

    Detection(GSHOTPyramid::Scalar score, int z, int y, int x, int lvl, Rectangle bndbox) : Rectangle(bndbox),
    score(score), x(x), y(y), z(z), lvl(lvl)
    {
    }

    bool operator<(const Detection & detection) const
    {
        return score > detection.score;
    }
};

class Test{
public:

    Test( char* sceneFilename, char* chairFilename, char* tableFilename) :
        sceneName( sceneFilename), tableName( tableFilename),
        sceneCloud( new PointCloudT), chairCloud( new PointCloudT), tableCloud( new PointCloudT)
    {


        if (pcl::io::loadPCDFile<PointType>(sceneFilename, *sceneCloud) == -1) {
            cout<<"test::couldnt open pcd file"<<endl;
        }
        PointCloudPtr tmpCloud( new PointCloudT);
        if (pcl::io::loadPCDFile<PointType>(chairFilename, *tmpCloud) == -1) {
            cout<<"test::couldnt open pcd file"<<endl;
        }
        if (pcl::io::loadPCDFile<PointType>(tableFilename, *tableCloud) == -1) {
            cout<<"test::couldnt open pcd file"<<endl;
        }


        sceneResolution = 25 * GSHOTPyramid::computeCloudResolution(sceneCloud);
        cout<<"test::sceneResolution : "<<sceneResolution<<endl;


        PointType minScene;
        PointType maxScene;
        pcl::getMinMax3D(*sceneCloud , minScene, maxScene);

        originScene = Vector3i(minScene.z/sceneResolution, minScene.y/sceneResolution, minScene.x/sceneResolution);
        Model::triple<int, int, int> sceneSize( (maxScene.z-minScene.z)/sceneResolution+1,
                                                (maxScene.y-minScene.y)/sceneResolution+1,
                                                (maxScene.x-minScene.x)/sceneResolution+1);
        sceneBox = Rectangle(originScene , sceneSize.first, sceneSize.second, sceneSize.third, sceneResolution);
        cout<<"test:: sceneBox = "<<sceneBox<<endl;
        cout<<"test:: minScene = "<<minScene<<endl;
        cout<<"test:: maxScene = "<<maxScene<<endl;

        viewer.addPC( sceneCloud);
        viewer.displayCubeLine(sceneBox);
        viewer.viewer->addLine(minScene,
                pcl::PointXYZ(sceneResolution*(originScene(2)+sceneSize.third),
                              sceneResolution*(originScene(1)+sceneSize.second),
                              sceneResolution*(originScene(0)+sceneSize.first)), "kjbij");




        Eigen::Affine3f transform = Eigen::Affine3f::Identity();

        // Define a translation of 2.5 meters on the x axis.
        transform.translation() << 1, 0.0, 0.0;
        pcl::transformPointCloud (*tmpCloud, *chairCloud, transform);


        PointType minChair;
        PointType maxChair;
        pcl::getMinMax3D(*chairCloud , minChair, maxChair);


        Model::triple<int, int, int> chairSize( (maxChair.z-minChair.z)/sceneResolution/2+1,
                                                (maxChair.y-minChair.y)/sceneResolution/2+1,
                                                (maxChair.x-minChair.x)/sceneResolution/2+1);
        chairBox = Rectangle(Eigen::Vector3i((minChair.z/*-minScene.z*/)/sceneResolution/2,
                           (minChair.y/*-minScene.y*/)/sceneResolution/2,
                           (minChair.x/*-minScene.x*/)/sceneResolution/2), chairSize.first, chairSize.second, chairSize.third, sceneResolution*2);


        cout<<"test:: chairBox = "<<chairBox<<endl;
        cout<<"test:: minChair = "<<minChair<<endl;
        cout<<"test:: maxChair = "<<maxChair<<endl;
        viewer.displayCubeLine(chairBox, Eigen::Vector3i(255,255,0));


        PointType minTable;
        PointType maxTable;
        pcl::getMinMax3D(*tableCloud , minTable, maxTable);

        Model::triple<int, int, int> tableSize( (maxTable.z-minTable.z)/sceneResolution/2+1,
                                                (maxTable.y-minTable.y)/sceneResolution/2+1,
                                                (maxTable.x-minTable.x)/sceneResolution/2+1);



        tableBox = Rectangle(Eigen::Vector3i(minTable.z/sceneResolution/2,
                                 minTable.y/sceneResolution/2,
                                 minTable.x/sceneResolution/2), tableSize.first, tableSize.second, tableSize.third, sceneResolution*2);

        cout<<"test:: tableBox = "<<tableBox<<endl;
        cout<<"test:: tableBox left = "<<tableBox.left()<<endl;
        cout<<"test:: tableBox right = "<<tableBox.right()<<endl;


    }

    void createChairModel(){

        int nbParts = 1;

        Model::triple<int, int, int> chairSize(chairBox.depth(), chairBox.height(), chairBox.width());
        Model::triple<int, int, int> chairPartSize( chairBox.depth()/2,
                                                    chairBox.height()/2,
                                                    chairBox.width()/2);

        Model model( chairSize, nbParts, chairPartSize);
        GSHOTPyramid pyramid(chairCloud, Eigen::Vector3i( 3,3,3));
        model.parts()[0].filter = pyramid.levels()[0].block( chairBox.origin()(0), chairBox.origin()(1), chairBox.origin()(2),
                                                             chairSize.first, chairSize.second, chairSize.third);
        model.parts()[1].filter = pyramid.levels()[0].block( 2, 0, 6, chairPartSize.first, chairPartSize.second, chairPartSize.third);

        ofstream out2("chairModel.txt");

        out2 << (model);
    }

    void testNegLatSearch(){
        cout << "testNegLatSearch ..." << endl;
        Model::triple<int, int, int> chairSize(chairBox.depth(), chairBox.height(), chairBox.width());
        Model::triple<int, int, int> chairPartSize( chairBox.depth()/2,
                                                    chairBox.height()/2,
                                                    chairBox.width()/2);

        cout<<"test::chairPartSize : "<<chairPartSize.first<<" "<<chairPartSize.second<<" "<<chairPartSize.third<<endl;

        Model model( chairSize, 1, chairPartSize);
        GSHOTPyramid pyramid(sceneCloud, Eigen::Vector3i( 3,3,3));
        std::vector<Model> models = { model};

        vector<Object> objects;
        Object obj(Object::CHAIR, Object::Pose::UNSPECIFIED, false, false, chairBox);
//        Object obj2(Object::CHAIR, Object::Pose::UNSPECIFIED, false, false, chairBox);
        objects.push_back(obj);
//        objects.push_back(obj2);

        vector<Scene> scenes = {Scene( originScene, sceneBox.depth(), sceneBox.height(), sceneBox.width(), sceneName, objects)};

        Mixture mixture( models);

        GSHOTPyramid::Level root2x =
                pyramid.levels()[0].block(chairBox.origin()(0)*2, chairBox.origin()(1)*2, chairBox.origin()(2)*2,
                                          chairSize.first*2, chairSize.second*2, chairSize.third*2);
        for(int mod = 0; mod<mixture.models().size(); ++mod){
            mixture.models()[mod].initializeParts( mixture.models()[mod].parts().size() - 1,
                                                           mixture.models()[mod].partSize(), root2x);
        }

        Model sample;
        sample.parts().resize(1);
        sample.parts()[0].filter = pyramid.levels()[0].block(chairBox.origin()(0), chairBox.origin()(1), chairBox.origin()(2),
                                                              chairSize.first, chairSize.second, chairSize.third);
        sample.parts()[0].offset.setZero();
        sample.parts()[0].deformation.setZero();
        mixture.models()[0].parts()[0].filter = sample.parts()[0].filter;


        mixture.zero_ = false;

        int interval = 1;
        int maxNegatives = 24000;
        vector<pair<Model, int> >  negatives;

        int j = 0;

        for (int i = 0; i < negatives.size(); ++i)
            if ((negatives[i].first.parts()[0].deformation(3) =
                 models[negatives[i].second].dot(negatives[i].first)) > -1.01)
                negatives[j++] = negatives[i];

        negatives.resize(j);
        mixture.negLatentSearch(scenes, Object::BICYCLE, Eigen::Vector3i( 3,3,3), interval, maxNegatives, negatives);
        cout<<"test::negatives.size = "<<negatives.size()<<endl;
        if(negatives.size()>0){
            cout<<"test::negatives[0].first.parts()[0].filter.isZero() : "<< GSHOTPyramid::TensorMap( negatives[0].first.parts()[0].filter).isZero() << endl;
            //    //offset Null for the root
            //    cout<<"test::positives[0].first.parts()[0].offset : "<< positives[0].first.parts()[1].offset << endl;
            //    cout<<"test::positives[0].first.parts()[0].deformation : "<< positives[0].first.parts()[1].deformation << endl;



            ofstream out("tmp.txt");

            out << (negatives[0].first);
        }
    }

    void testPosLatSearch(){

        int nbParts = 1;
        int nbLevel = 2;
        Model::triple<int, int, int> chairSize(chairBox.depth(), chairBox.height(), chairBox.width());
        Model::triple<int, int, int> chairPartSize( chairBox.depth()/2,
                                                    chairBox.height()/2,
                                                    chairBox.width()/2);

        cout<<"test::chairPartSize : "<<chairPartSize.first<<" "<<chairPartSize.second<<" "<<chairPartSize.third<<endl;

//        Rectangle chairBox2( chairBox);


        Model model( chairSize, nbParts, chairPartSize);
//        GSHOTPyramid pyramid(sceneCloud, Eigen::Vector3i( 3,3,3));
//        model.parts()[0].filter = pyramid.levels()[0].block( 6, 1, 1, chairSize.first, chairSize.second, chairSize.third);
//        model.parts()[1].filter = pyramid.levels()[0].block( 7, 2, 1, chairPartSize.first, chairPartSize.second, chairPartSize.third);
        std::vector<Model> models = { model};

        vector<Object> objects;
        Object obj(Object::CHAIR, Object::Pose::UNSPECIFIED, false, false, chairBox);
        Object obj2(Object::CHAIR, Object::Pose::UNSPECIFIED, false, false, tableBox);
        objects.push_back(obj);
        objects.push_back(obj2);

        vector<Scene> scenes = {Scene( originScene, sceneBox.depth(), sceneBox.height(), sceneBox.width(), sceneName, objects)};


        Mixture mixture( models);


        int interval = 1;
        float overlap = 0.4;
        vector<pair<Model, int> > positives;

        mixture.posLatentSearch(scenes, Object::CHAIR, Eigen::Vector3i( 3,3,3), interval, overlap, positives);

        cout<<"test::positives.size : "<< positives.size() << endl;
        mixture.zero_ = false;
        cout<<"test:: set zero_ to false" << endl;

        Model sample;
        GSHOTPyramid pyramid(sceneCloud, Eigen::Vector3i( 3,3,3), interval);
        vector<vector<Model::Positions> > positions;
        positions.resize(nbParts);
        for(int i=0; i<nbParts; ++i){
            (positions)[i].resize(nbLevel);
        }

//        mixture.models()[0].initializeSample(pyramid, chairBox.origin()(2), chairBox.origin()(1), chairBox.origin()(0), 0, sample, &positions);//x, y, z, lvl
        sample.parts().resize(1);
        sample.parts()[0].filter = pyramid.levels()[0].block(chairBox.origin()(0), chairBox.origin()(1), chairBox.origin()(2),
                                                              chairSize.first, chairSize.second, chairSize.third);
        sample.parts()[0].offset.setZero();
        sample.parts()[0].deformation.setZero();
        mixture.models()[0].parts()[0].filter = sample.parts()[0].filter;


        cout<<"test:: sample.parts()[0].filter.isZero() : "
           << GSHOTPyramid::TensorMap( sample.parts()[0].filter).isZero() << endl;
        cout<<"test:: mixture.models()[0].parts()[0].filter.isZero() : "
           << GSHOTPyramid::TensorMap( mixture.models()[0].parts()[0].filter).isZero() << endl;



        mixture.posLatentSearch(scenes, Object::CHAIR, Eigen::Vector3i( 3,3,3), interval, overlap, positives);


        if(positives.size()>0) {
            cout<<"test::positives[0].first.parts()[0].filter.isZero() : "<< GSHOTPyramid::TensorMap( positives[0].first.parts()[0].filter).isZero() << endl;
    //    //offset Null for the root
            cout<<"test::positives[0].first.parts()[0].offset : "<< positives[0].first.parts()[1].offset << endl;
    //    cout<<"test::positives[0].first.parts()[0].deformation : "<< positives[0].first.parts()[1].deformation << endl;
            ofstream out("tmp.txt");
            out << (positives[0].first);

            Eigen::Vector3i origin(-2, -1, 4);//check comment : Mix:PosLatentSearch found a positive sample at : -2 -1 4 / 0.169058

            Rectangle boxFound(origin, chairSize.first, chairSize.second, chairSize.third, sceneResolution*2);
            viewer.displayCubeLine(boxFound, Eigen::Vector3i(255,0,255));

            //Mix:PosLatentSearch found a positive sample with offsets : -2 -3 -3
            Eigen::Vector3i partOrigin(origin(0) * 2 + positives[0].first.parts()[1].offset(0)/*-2*2*/,
                                       origin(1) * 2 + positives[0].first.parts()[1].offset(1)/*-3*2*/,
                                       origin(2) * 2 + positives[0].first.parts()[1].offset(2)/*-3*2*/);
            Rectangle partsBoxFound(partOrigin, chairPartSize.first, chairPartSize.second, chairPartSize.third, sceneResolution);

            viewer.displayCubeLine(partsBoxFound, Eigen::Vector3i(0,255,0));

        }



    }

    void testTrain(){


        Model::triple<int, int, int> chairSize(chairBox.depth(), chairBox.height(), chairBox.width());
        Model::triple<int, int, int> chairPartSize( chairBox.depth()/2,
                                                    chairBox.height()/2,
                                                    chairBox.width()/2);

        cout<<"test::chairPartSize : "<<chairPartSize.first<<" "<<chairPartSize.second<<" "<<chairPartSize.third<<endl;

        Model model( chairSize, 0);
        std::vector<Model> models = { model};

        vector<Object> objects, objects2;
        Object obj(Object::CHAIR, Object::Pose::UNSPECIFIED, false, false, chairBox);
        Object obj2(Object::TRAIN, Object::Pose::UNSPECIFIED, false, false, tableBox);

        objects.push_back(obj);
        objects2.push_back(obj2);

        vector<Scene> scenes = {Scene( originScene, sceneBox.depth(), sceneBox.height(), sceneBox.width(), sceneName, objects)/*,
                                Scene( originScene, sceneBox.depth(), sceneBox.height(), sceneBox.width(), tableName, objects2)*/};


        Mixture mixture( models);

        int interval = 1, nbIterations = 2, nbDatamine = 10, maxNegSample = 10;
        mixture.train(scenes, Object::CHAIR, Eigen::Vector3i( 3,3,3), interval, nbIterations/2, nbDatamine, maxNegSample);

        cout << "test:: root filter initialized" << endl;

        GSHOTPyramid pyramid(sceneCloud, Eigen::Vector3i( 3,3,3), interval);
        GSHOTPyramid::Level root2x =
                pyramid.levels()[0].block(chairBox.origin()(0)*2, chairBox.origin()(1)*2, chairBox.origin()(2)*2,
                                          chairSize.first*2, chairSize.second*2, chairSize.third*2);

        mixture.initializeParts( 1, chairPartSize, root2x);


        mixture.train(scenes, Object::CHAIR, Eigen::Vector3i( 3,3,3), interval, nbIterations/2, nbDatamine, maxNegSample);

        Eigen::Vector3i origin(-2, -1, 4);//check comment : Mix:PosLatentSearch found a positive sample at : -2 -1 4 / 0.169058

        Rectangle boxFound(origin, chairSize.first, chairSize.second, chairSize.third, sceneResolution*2);
        viewer.displayCubeLine(boxFound, Eigen::Vector3i(255,0,255));

        //Mix:PosLatentSearch found a positive sample with offsets : -2 -3 -3
        Eigen::Vector3i partOrigin(origin(0) * 2 + mixture.models()[0].parts()[1].offset(0)/*-2*2*/,
                                   origin(1) * 2 + mixture.models()[0].parts()[1].offset(1)/*-3*2*/,
                                   origin(2) * 2 + mixture.models()[0].parts()[1].offset(2)/*-3*2*/);
        Rectangle partsBoxFound(partOrigin, chairPartSize.first, chairPartSize.second, chairPartSize.third, sceneResolution);

        viewer.displayCubeLine(partsBoxFound, Eigen::Vector3i(0,255,0));



    }

    void detect(const Mixture & mixture, /*int depth, int height, int width, */int interval, const GSHOTPyramid & pyramid,
                double threshold, double overlap,/* const string image, */ostream & out,
                const string & images, vector<Detection> & detections, const Scene * scene = 0,
                Object::Name name = Object::UNKNOWN)
    {
        // Compute the scores
        vector<Tensor3DF> scores;
        vector<Mixture::Indices> argmaxes;
        vector<vector<vector<Model::Positions> > > positions;

        mixture.computeEnergyScores( pyramid, scores, argmaxes, &positions);

        // Cache the size of the models
        vector<Model::triple<int, int, int> > sizes(mixture.models().size());

        for (int i = 0; i < sizes.size(); ++i)
            sizes[i] = mixture.models()[i].rootSize();


        // For each scale
        for (int lvl = 0; lvl < scores.size(); ++lvl) {
//            const double scale = pow(2.0, static_cast<double>(lvl) / pyramid.interval() + 2);
            const double scale = 1 / pow(2.0, static_cast<double>(lvl) / interval);
            int offz = scene->origin()(0)*scale;
            int offy = scene->origin()(1)*scale;
            int offx = scene->origin()(2)*scale;

            cout<<"test:: offz = "<<offz<<endl;
            cout<<"test:: offy = "<<offy<<endl;
            cout<<"test:: offx = "<<offx<<endl;

            const int depths = static_cast<int>(scores[lvl].depths());
            const int rows = static_cast<int>(scores[lvl].rows());
            const int cols = static_cast<int>(scores[lvl].cols());

            cout<<"test:: for lvl "<<lvl<<" :"<<endl;

            cout<<"test:: scores[lvl].depths() = "<<depths<<endl;
            cout<<"test:: scores[lvl].rows() = "<<rows<<endl;
            cout<<"test:: scores[lvl].cols() = "<<cols<<endl;

            ofstream out("conv.txt");

            out << scores[lvl]();

            for (int z = 0; z < depths; ++z) {
                for (int y = 0; y < rows; ++y) {
                    for (int x = 0; x < cols; ++x) {
                        const double score = scores[lvl]()(z, y, x);

                        cout<<"test:: scores = "<<score<<endl;

                        //TODO !!!!!
                        if (score > threshold) {
                            // Non-maxima suppresion in a 3x3 neighborhood
//                            if (((y == 0) || (x == 0) || (score >= scores[lvl]()(z, y - 1, x - 1))) &&
//                                ((y == 0) || (score >= scores[lvl]()(z, y - 1, x))) &&
//                                ((y == 0) || (x == cols - 1) || (score >= scores[lvl]()(z, y - 1, x + 1))) &&
//                                ((x == 0) || (score >= scores[lvl]()(z, y, x - 1))) &&
//                                ((x == cols - 1) || (score >= scores[lvl]()(z, y, x + 1))) &&
//                                ((y == rows - 1) || (x == 0) || (score >= scores[lvl]()(z, y + 1, x - 1))) &&
//                                ((y == rows - 1) || (score >= scores[lvl]()(z, y + 1, x))) &&
//                                ((y == rows - 1) || (x == cols - 1) ||
//                                 (score >= scores[lvl]()(z + 1, y + 1, x + 1)))) {



                                Eigen::Vector3i origin((z+offz)/*- pad.z()*/,
                                                       (y+offy)/*- pad.y()*/,
                                                       (x+offx)/* - pad.x()*/);
                                int w = sizes[argmaxes[lvl]()(z, y, x)].third /** scale*/;
                                int h = sizes[argmaxes[lvl]()(z, y, x)].second /** scale*/;
                                int d = sizes[argmaxes[lvl]()(z, y, x)].first /** scale*/;

                                Rectangle bndbox( origin, d, h, w, pyramid.resolutions()[lvl]);//indices of the cube in the PC

                                cout<<"test:: detection bndbox = "<<bndbox<<endl;


//                                Rectangle bndbox((x - pyramid.padx()) * scale + 0.5,
//                                                 (y - pyramid.pady()) * scale + 0.5,
//                                                 sizes[argmaxes[lvl](y, x)].second * scale + 0.5,
//                                                 sizes[argmaxes[lvl](y, x)].first * scale + 0.5);

//                                // Truncate the object
//                                bndbox.setX(max(bndbox.x(), 0));
//                                bndbox.setY(max(bndbox.y(), 0));
//                                bndbox.setWidth(min(bndbox.width(), width - bndbox.x()));
//                                bndbox.setHeight(min(bndbox.height(), height - bndbox.y()));

                                if (!bndbox.empty()){
                                    detections.push_back(Detection(score, z, y, x, lvl, bndbox));
                                    cout<<"test:: bndbox added to detections"<<endl;
                                }

                        }
                    }
                }
            }
        }

        cout<<"test:: detections.size = "<<detections.size()<<endl;
        // Non maxima suppression
        sort(detections.begin(), detections.end());

        for (int i = 1; i < detections.size(); ++i)
            detections.resize(remove_if(detections.begin() + i, detections.end(),
                                        Intersector(detections[i - 1], overlap, true)) - detections.begin());


        cout<<"test:: detections.size after intersection = "<<detections.size()<<endl;

        // Draw the detections
        if (!images.empty()) {
//            JPEGImage im(image);

            for (int i = 0; i < detections.size(); ++i) {
                // Find out if the detection hits an object
//                bool positive = false;

//                if (scene) {
//                    Intersector intersector(detections[i]);

//                    for (int j = 0; j < scene->objects().size(); ++j)
//                        if (scene->objects()[j].name() == name)
//                            if (intersector(scene->objects()[j].bndbox()))
//                                positive = true;
//                }


                const int x = detections[i].x;
                const int y = detections[i].y;
                const int z = detections[i].z;
                const int lvl = detections[i].lvl;

                const int argmax = argmaxes[lvl]()(z, y, x);

                cout<<"test:: detections[i].score = "<<detections[i].score<<endl;

                //draw each parts
                for (int j = 0; j < positions[argmax].size(); ++j) {
                    const int zp = positions[argmax][j][lvl]()(z, y, x)(0);
                    const int yp = positions[argmax][j][lvl]()(z, y, x)(1);
                    const int xp = positions[argmax][j][lvl]()(z, y, x)(2);
                    const int lvlp = positions[argmax][j][lvl]()(z, y, x)(3);

                    cout<<"test:: positions[argmax][j][lvl]()(z, y, x) = "<<positions[argmax][j][lvl]()(z, y, x)<<endl;


//                    const double scale = pow(2.0, static_cast<double>(zp) / pyramid.interval() + 2);
//                    const double scale = 1 / pow(2.0, static_cast<double>(lvlp) / interval);


                    Eigen::Vector3i origin((zp + detections[i].origin()(0))/*- pad.z()*/,
                                           (yp + detections[i].origin()(1))/*- pad.y()*/,
                                           (xp + detections[i].origin()(2))/* - pad.x()*/);
                    int w = mixture.models()[argmax].partSize().third *2/** scale*/;
                    int h = mixture.models()[argmax].partSize().second *2/** scale*/;
                    int d = mixture.models()[argmax].partSize().first *2/** scale*/;

                    Rectangle bndbox( origin, d, h, w, pyramid.resolutions()[lvlp]);//indices of the cube in the PC

                    cout<<"test:: part bndbox to draw = "<<bndbox<<endl;


//                    const Rectangle bndbox((xp - pyramid.padx()) * scale + 0.5,
//                                           (yp - pyramid.pady()) * scale + 0.5,
//                                           mixture.models()[argmax].partSize().second * scale + 0.5,
//                                           mixture.models()[argmax].partSize().first * scale + 0.5);


                    viewer.displayCubeLine(bndbox, Vector3i(255,0,255));
//               352     draw(im, bndbox, 0, 0, 255, 2);
                }

                // Draw the root last
                cout<<"test:: root bndbox = "<<detections[i]<<endl;
                Rectangle box(Vector3i(detections[i].origin()(0), detections[i].origin()(1), detections[i].origin()(2)),
                              mixture.models()[argmax].rootSize().first, mixture.models()[argmax].rootSize().second,
                              mixture.models()[argmax].rootSize().third, pyramid.resolutions()[1]);
                viewer.displayCubeLine(box, Vector3i(0,255,255));
//                draw(im, detections[i], positive ? 0 : 255, positive ? 255 : 0, 0, 2);
            }

        }
    }


    void testTest(){


        ifstream in("tmp.txt");

        if (!in.is_open()) {
            cerr << "Cannot open model file\n" << endl;
            return;
        }

        Mixture mixture;
        in >> mixture;

        if (mixture.empty()) {
            cerr << "Invalid model file\n" << endl;
            return;
        }

        cout<<mixture.models()[0].parts()[1].offset<<endl;

        int interval = 1;
        float threshold=0.1, overlap=0.5;
        GSHOTPyramid pyramid(sceneCloud, Eigen::Vector3i( 3,3,3), interval);

        ofstream out("tmpTest.txt");
        string images = sceneName;
        vector<Detection> detections;
        Object obj(Object::CHAIR, Object::Pose::UNSPECIFIED, false, false, chairBox);
        Scene scene( originScene, sceneBox.depth(), sceneBox.height(), sceneBox.width(), sceneName, {obj});

        detect(mixture, /*0, image.width(), image.height()*/interval, pyramid, threshold, overlap, /*file, */out,
               images, detections, &scene, Object::CHAIR);

    }

    void testTrainSVM(){

//        Model::triple<int, int, int> rootSize( sceneSize.first/4,
//                                           sceneSize.second/4,
//                                               sceneSize.third/4);
//        Model::triple<int, int, int> partSize( sceneSize.first/6,
//                                               sceneSize.second/6,
//                                               sceneSize.third/6);
//        Rectangle sceneRec( Eigen::Vector3i(1, 1, 1), rootSize.first, rootSize.second, rootSize.third, resolution);
//        Rectangle sceneRec2( Eigen::Vector3i(3, 5, 4), rootSize.first, rootSize.second, rootSize.third, resolution);
////        Rectangle sceneRec3( Eigen::Vector3i(2, 2, 2), rootSize.third*0.75, rootSize.second*2, rootSize.first);

//        cout<<"test::sceneRec : "<< sceneRec <<endl;
//        cout<<"test::sceneRec2 : "<< sceneRec2 <<endl;
//        cout<<"test::rootSize : "<<rootSize.first<<" "<<rootSize.second<<" "<<rootSize.third<<endl;
//        cout<<"test::partSize : "<<partSize.first<<" "<<partSize.second<<" "<<partSize.third<<endl;

//        vector<pair<Model, int> >  positives, negatives;
//        Model modelScene( rootSize, 1, partSize);
//        GSHOTPyramid pyramid(sceneCloud, Eigen::Vector3i( 3,3,3));

//        modelScene.parts()[0].filter = pyramid.levels()[0].block( 1, 1, 1, rootSize.first, rootSize.second, rootSize.third);
//        modelScene.parts()[1].filter = pyramid.levels()[0].block( 2, 2, 1, partSize.first, partSize.second, partSize.third);

//        positives.push_back(pair<Model, int>(modelScene, 0));

//        modelScene.parts()[0].filter = pyramid.levels()[0].block( 5, 5, 5, rootSize.first, rootSize.second, rootSize.third);
//        modelScene.parts()[1].filter = pyramid.levels()[0].block( 5, 5, 5, partSize.first, partSize.second, partSize.third);

//        negatives.push_back(pair<Model, int>(modelScene, 0));


//        Model model( rootSize, 1, partSize);
//        std::vector<Model> models = { model};

//        Mixture mixture( models);

//        double C = 0.002;
//        double J = 2.0;
//        mixture.trainSVM(positives, negatives, C, J);

//        ofstream out("tmp.txt");

//        out << (mixture);

//        ////////////

//        modelScene.parts()[0].filter = pyramid.levels()[0].block( 5, 1, 1, rootSize.first, rootSize.second, rootSize.third);
//        modelScene.parts()[1].filter = pyramid.levels()[0].block( 5, 2, 1, partSize.first, partSize.second, partSize.third);

//        positives[0] = pair<Model, int>(modelScene, 0);

//        modelScene.parts()[0].filter = pyramid.levels()[0].block( 0, 0, 5, rootSize.first, rootSize.second, rootSize.third);
//        modelScene.parts()[1].filter = pyramid.levels()[0].block( 1, 1, 5, partSize.first, partSize.second, partSize.third);

//        negatives[0] = pair<Model, int>(modelScene, 0);

//        mixture.trainSVM(positives, negatives, C, J);

//        ofstream out2("tmp2.txt");

//        out2 << (mixture);
    }

    char* sceneName;
    char* tableName;
    PointCloudPtr sceneCloud;
    PointCloudPtr chairCloud;
    PointCloudPtr tableCloud;
    Vector3i originScene;
    Rectangle sceneBox;
    Rectangle chairBox;
    Rectangle tableBox;
    float starting_resolution;
    float sceneResolution;
    Viewer viewer;
};


int main(){
    //Turn pcl message to OFF !!!!!!!!!!!!!!!!!!!!
    pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);


    Test test( "/home/ubuntu/3DDataset/3DDPM/smallScene.pcd", "/home/ubuntu/3DDataset/3DDPM/chair.pcd", "/home/ubuntu/3DDataset/3DDPM/table.pcd");

//    test.testTrainSVM();//OK
    //TODO test trainSVM because train doesnt update filters. Maybe because it looks for null filters during posLatentSearch ???
//    test.testPosLatSearch();
//    test.testNegLatSearch();

//    test.createChairModel();

//    test.initSample();

    test.testTrain();

//    test.testTest();

    test.viewer.viewer->addLine(pcl::PointXYZ(0,-1,0), pcl::PointXYZ(2*0.0845292, -1, 0),"ijbij");

    test.viewer.show();




    return 0;
}

