#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedDeleteInstance){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a VO with no authentication
	auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/Instance_ABCD");
	ENSURE_EQUAL(delResp.status,403,
				 "Requests to delete instances without authentication should be rejected");
	
	//try deleting a VO with invalid authentication
	delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/Instance_ABCD?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(delResp.status,403,
				 "Requests to delete instances with invalid authentication should be rejected");

}

TEST(DeleteInstance){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/InstanceListResultSchema.json");
	
	std::string voName="test-inst-delete";
	std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	std::string instID;
	{ //install an instance
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
	}
	
	{ //delete the instance
		auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/"+instID+"?token="+adminKey);
		ENSURE_EQUAL(delResp.status,200,"Instance deletion request should succeed");
	}
}

TEST(UnrelatedUserDeleteInstance){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/InstanceListResultSchema.json");
	
	std::string voName="test-inst-delete";
	std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	
	{ //install an instance
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
	}
	
	std::string tok;
	{ //create an unrelated user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	{ //delete the instance
		auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/"+instID+"?token="+tok);
		ENSURE_EQUAL(delResp.status,403,
		            "Requests to delete application instances from unrelated users should be rejected");
	}
}