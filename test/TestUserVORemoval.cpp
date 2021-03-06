#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedRemoveUserFromVO){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a user with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/vos/VO_123");
	ENSURE_EQUAL(delResp.status,403,
				 "Requests to users to VOs without authentication should be rejected");
	
	//try deleting a user with invalid authentication
	delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/vos/VO_123?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(delResp.status,403,
				 "Requests to add users to VOs with invalid authentication should be rejected");
}

TEST(RemoveUserFromVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
	}
	
	{ //add the user to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //remove the user from the VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,200,"User removal from VO request should succeed");
	}
	
	{ //check that the user is no longer in the VO
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's information should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["metadata"]["VOs"].Size(),0,"User should belong to no VOs");
	}
}

TEST(UserRemoveSelfFromVO){ //non-admins should be able to remove themselves from VOs
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid;
	std::string tok;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	{ //add the user to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //have the user remove itself from the VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+tok);
		ENSURE_EQUAL(remResp.status,200,"User removal from VO request should succeed");
	}
	
	{ //check that the user is no longer in the VO
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's information should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["metadata"]["VOs"].Size(),0,"User should belong to no VOs");
	}
}

TEST(UserRemoveOtherFromVO){
	//non-admins should be able to remove others from VOs of which they are members
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid1;
	std::string tok1;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid1=createData["metadata"]["id"].GetString();
		tok1=createData["metadata"]["access_token"].GetString();
	}
	
	std::string uid2;
	{ //create another user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Fred", alloc);
		metadata.AddMember("email", "fred@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Fred's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid2=createData["metadata"]["id"].GetString();
	}
	
	{ //add the first user to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid1+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //add the second user to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //have the first user remove the second from the VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"/vos/"+voName+"?token="+tok1);
		ENSURE_EQUAL(remResp.status,200,"User removal from VO request should succeed");
	}
	
	{ //check that the second user is no longer in the VO
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's information should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["metadata"]["VOs"].Size(),0,"User should belong to no VOs");
	}
}

//This test turns out not to be useful; in the current implementation these 
//operations trivial succeed, rather than being rejected for being redundant.
/*
TEST(RemoveUserFromVONotMember){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
	}
	
	std::string voName="some-org";
	
	{ //attempt to remove the user from a VO to which it does not belong, 
		//when the VO in question also does not exist
		auto addResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey);
		ENSURE_EQUAL(addResp.status,404,
		             "User removal from non-existent VO request should be rejected");
	}
	
	{ //create the VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //attempt to remove the user from a VO to which it does not belong, 
		//although the VO does exist
		auto addResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey);
		ENSURE_EQUAL(addResp.status,404,
		             "User removal from non-existent VO request should be rejected");
	}
}
*/

TEST(RemoveNonexistentUserFromVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid="User_2375627864987598275";
	std::string voName="some-org";
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //attempt to remove a nonexistent user from the VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,404,
		             "Request to remove non-existent user from a VO should be rejected");
	}
}

TEST(NonmemberRemoveOtherFromVO){
	//non-admins should not be able to remove others from VOs of which they are not members
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid1;
	std::string tok1;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid1=createData["metadata"]["id"].GetString();
		tok1=createData["metadata"]["access_token"].GetString();
	}
	
	std::string uid2;
	{ //create another user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Fred", alloc);
		metadata.AddMember("email", "fred@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Fred's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid2=createData["metadata"]["id"].GetString();
	}
	
	{ //add the second user to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //have the first user attempt remove the second from the VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"/vos/"+voName+"?token="+tok1);
		ENSURE_EQUAL(remResp.status,403,
		             "User removal from VO request frm non-member should be rejected");
	}
	
	{ //check that the second user is still longer in the VO
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's information should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["metadata"]["VOs"].Size(),1,"User should belong to one VO");
		ENSURE_EQUAL(data["metadata"]["VOs"][0].GetString(),voName,"User should belong to the correct VO");
	}
}
