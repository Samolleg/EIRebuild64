// Included inside namespace Navigation by Query.cpp. This uses the same support
// resolution and clearance policy as the game's navigation, but never uses
// the mesh's mutable game query or its scratch buffers.
struct LiveQuery::State {
    Point input{}, requestedGoal{}, near{}, nearGoal{}, actual{};
    bool path=false, ingress=false;
    unsigned stage=0;
    std::unique_ptr<dtNavMeshQuery,QueryDeleter> query;
    dtQueryFilter filter;
    dtPolyRef from=0, to=0;
    float start[3]{}, end[3]{};
    std::array<dtPolyRef,512> corridor{};
    std::array<float,256*3> straight{};
    std::array<unsigned char,256> flags{};
    std::array<dtPolyRef,256> refs{};
    int straightCount=0, point=0;
    dtStatus status=DT_FAILURE;
    Result result;
    State(Point p,const Point* goal,std::unique_ptr<dtNavMeshQuery,QueryDeleter> q)
        : input(p),path(goal!=nullptr),query(std::move(q)) {
        if(goal)requestedGoal=*goal;
        result.points.reserve(straight.size()/3+1);
    }
    bool Fail(Status reason) { result.status=reason;stage=99;return true; }
};
LiveQuery::LiveQuery(Point source,const Point* goal,std::unique_ptr<dtNavMeshQuery,QueryDeleter> query)
    : state(std::make_unique<State>(source,goal,std::move(query))) {}
LiveQuery::~LiveQuery()=default;
const LiveQuery::Result& LiveQuery::Get() const noexcept {return state->result;}
bool LiveQuery::Step(const Mesh& mesh) {
    auto& s=*state;
    if(s.stage==99)return true;
    if(!mesh.data||!mesh.tiles)return s.Fail(Status::NoMesh);
    if(!Finite(s.input)||(s.path&&!Finite(s.requestedGoal)))return s.Fail(Status::Invalid);
    const float height=SelectedClearance(mesh.profile,LegacyClassHeight(mesh.profile.materialClass));
    switch(s.stage) {
    case 0: {
        if(!s.query||dtStatusFailed(s.query->init(mesh.data.get(),65535)))return s.Fail(Status::Limit);
        const auto mask=ClearanceMask(mesh.profile,height);if(!mask)return s.Fail(Status::Invalid);
        s.filter.setIncludeFlags(mask);s.filter.setExcludeFlags(ClassFlags(mesh,0));
        s.stage=1;return false;
    }
    case 1: {
        auto& q=*s.query;
        if(!s.path) {
            if(!Nearest(mesh,q,s.input,.001f,4.f,s.filter,s.from,s.near,0,height))return s.Fail(Status::NoStart);
            s.result.source=Address(mesh,s.near,s.from,height,s.input.z,&q);
            if(!s.result.source.surface)return s.Fail(Status::NoStart);
            s.result.status=Status::Complete;s.stage=99;return true;
        }
        if(!Start(mesh,q,s.input,s.filter,s.from,s.near,s.actual,s.ingress,height))return s.Fail(Status::NoStart);
        s.result.source=Address(mesh,s.near,s.from,height,s.input.z,&q);
        if(!s.result.source.surface)return s.Fail(Status::NoStart);
        s.stage=2;return false;
    }
    case 2: {
        auto& q=*s.query;
        if(!Nearest(mesh,q,s.requestedGoal,.15f,4.f,s.filter,s.to,s.nearGoal,0,height))return s.Fail(Status::NoGoal);
        s.result.goal=Address(mesh,s.nearGoal,s.to,height,s.requestedGoal.z,&q);
        if(!s.result.goal.surface)return s.Fail(Status::NoGoal);
        s.start[0]=s.near.x;s.start[1]=s.near.y;s.start[2]=s.near.z;
        s.end[0]=s.nearGoal.x;s.end[1]=s.nearGoal.y;s.end[2]=s.nearGoal.z;
        s.status=q.initSlicedFindPath(s.from,s.to,s.start,s.end,&s.filter);
        if(dtStatusFailed(s.status))return s.Fail(Status::NoPath);
        s.stage=3;return false;
    }
    case 3: {
        int done=0;s.status=s.query->updateSlicedFindPath(4,&done);
        if(dtStatusInProgress(s.status)&&done>0)return false;
        if(dtStatusInProgress(s.status)||Limited(s.status))return s.Fail(Status::Limit);
        if(dtStatusFailed(s.status)||(!(s.from==s.to)&&!s.query->isInClosedList(s.to)))return s.Fail(Status::NoPath);
        s.stage=4;return false;
    }
    case 4: {
        int count=0;const auto status=s.query->finalizeSlicedFindPath(s.corridor.data(),&count,int(s.corridor.size()));
        if(Limited(status)){s.result.truncated=true;return s.Fail(Status::Limit);}
        if(dtStatusFailed(status)||dtStatusDetail(status,DT_PARTIAL_RESULT)||!count||s.corridor[count-1]!=s.to)return s.Fail(Status::NoPath);
        const auto straight=s.query->findStraightPath(s.start,s.end,s.corridor.data(),count,s.straight.data(),s.flags.data(),s.refs.data(),&s.straightCount,int(s.flags.size()),DT_STRAIGHTPATH_ALL_CROSSINGS);
        if(Limited(straight)){s.result.truncated=true;return s.Fail(Status::Limit);}
        if(dtStatusFailed(straight)||s.straightCount<1||!(s.flags[s.straightCount-1]&DT_STRAIGHTPATH_END))return s.Fail(Status::NoPath);
        if(s.ingress){auto at=Address(mesh,ToNav(s.actual),s.from,height,s.input.z,s.query.get());if(!at.surface)return s.Fail(Status::NoStart);s.result.points.push_back(at);}
        s.stage=5;return false;
    }
    case 5: {
        const int i=s.point++;
        const Point p{s.straight[i*3],s.straight[i*3+1],s.straight[i*3+2]};
        const float z=s.result.points.empty()?s.input.z:s.result.points.back().position.z;
        auto at=Address(mesh,p,s.refs[i]?s.refs[i]:s.to,height,z,s.query.get());
        if(!at.surface){s.result.points.clear();return s.Fail(Status::NoPath);}
        s.result.points.push_back(at);
        if(s.point<s.straightCount)return false;
        s.result.status=Status::Complete;s.stage=99;return true;
    }
    default:return s.Fail(Status::Invalid);
    }
}
